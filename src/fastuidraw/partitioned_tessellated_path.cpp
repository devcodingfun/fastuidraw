/*!
 * \file partitioned_tessellated_path.cpp
 * \brief file partitioned_tessellated_path.cpp
 *
 * Copyright 2019 by Intel.
 *
 * Contact: kevin.rogovin@intel.com
 *
 * This Source Code Form is subject to the
 * terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with
 * this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 *
 * \author Kevin Rogovin <kevin.rogovin@intel.com>
 *
 */

#include <vector>
#include <complex>
#include <algorithm>

#include <fastuidraw/tessellated_path.hpp>
#include <fastuidraw/path.hpp>
#include <fastuidraw/partitioned_tessellated_path.hpp>

#include <private/util_private.hpp>
#include <private/util_private_ostream.hpp>
#include <private/bounding_box.hpp>
#include <private/path_util_private.hpp>
#include <private/clip.hpp>

namespace
{
  enum
    {
      splitting_threshhold = 50,
      max_recursion_depth = 10
    };

  class ChainBuilder:fastuidraw::noncopyable
  {
  public:
    typedef fastuidraw::PartitionedTessellatedPath::segment segment;

    class PerChain
    {
    public:
      explicit
      PerChain(unsigned int start):
        m_range(start, start),
        m_has_before_first(false)
      {}

      PerChain(unsigned int start,
               const segment &before_first):
        m_range(start, start),
        m_before_first(before_first),
        m_has_before_first(true)
      {}

      fastuidraw::range_type<unsigned int> m_range;
      segment m_before_first;
      bool m_has_before_first;
    };

    const segment*
    add_segment(const segment &S)
    {
      FASTUIDRAWassert(!m_chains.empty());
      m_segments.push_back(S);
      m_bbox.union_point(S.m_start_pt);
      m_bbox.union_point(S.m_end_pt);
      ++m_chains.back().m_range.m_end;

      return &m_segments.back();
    }

    void
    start_chain(const segment *before_chain)
    {
      if (before_chain)
        {
          m_chains.push_back(PerChain(m_segments.size(), *before_chain));
        }
      else
        {
          m_chains.push_back(PerChain(m_segments.size()));
        }
    }

    fastuidraw::BoundingBox<float> m_bbox;
    std::vector<segment> m_segments;
    std::vector<PerChain> m_chains;
  };

  class ScratchSpacePrivate:fastuidraw::noncopyable
  {
  public:
    std::vector<fastuidraw::vec3> m_adjusted_clip_eqs;
    fastuidraw::c_array<const fastuidraw::vec2> m_clipped_rect;

    fastuidraw::vecN<std::vector<fastuidraw::vec2>, 2> m_clip_scratch_vec2s;
  };

  class SubsetPrivate:fastuidraw::noncopyable
  {
  public:
    typedef fastuidraw::PartitionedTessellatedPath::segment segment;
    typedef fastuidraw::PartitionedTessellatedPath::segment_chain segment_chain;

    ~SubsetPrivate();

    static
    SubsetPrivate*
    create(const fastuidraw::TessellatedPath &P,
           std::vector<SubsetPrivate*> &out_values);

    const fastuidraw::Rect&
    bounding_box(void) const
    {
      return m_bounding_box.as_rect();
    }

    bool
    has_children(void) const
    {
      return m_children[0] != nullptr;
    }

    SubsetPrivate*
    child(unsigned int I)
    {
      return m_children[I];
    }

    unsigned int
    ID(void) const
    {
      return m_ID;
    }

    fastuidraw::c_array<const segment_chain>
    segment_chains(void) const
    {
      return fastuidraw::make_c_array(m_chains);
    }

    unsigned int
    select_subsets(ScratchSpacePrivate &scratch,
                   fastuidraw::c_array<const fastuidraw::vec3> clip_equations,
                   const fastuidraw::float3x3 &clip_matrix_local,
                   const fastuidraw::vec2 &one_pixel_width,
                   float pixels_additional_room,
                   float item_space_additional_room,
                   fastuidraw::c_array<unsigned int> dst);

  private:
    /* creation will steal the vector<> values of
     * the passed ChainBuilder
     */
    SubsetPrivate(int recursion_depth, ChainBuilder &chains,
                  std::vector<SubsetPrivate*> &out_values);

    void
    make_children(int recursion_depth, std::vector<SubsetPrivate*> &out_values);

    int
    choose_splitting_coordinate(float &split_value);

    static
    void
    process_chain(int splitting_coordinate, float splitting_value,
                  const segment_chain &chain,
                  ChainBuilder *before_chain, ChainBuilder *after_chain);

    bool //returns true if this is added to dst
    select_subsets_implement(ScratchSpacePrivate &scratch,
                             fastuidraw::c_array<unsigned int> dst,
                             float item_space_additional_room,
                             unsigned int &current);

    bool //returns true if this is added to dst
    select_subsets_all_unculled(fastuidraw::c_array<unsigned int> dst,
                                unsigned int &current);

    unsigned int m_ID;
    fastuidraw::vecN<SubsetPrivate*, 2> m_children;
    std::vector<segment> m_segments;
    std::vector<segment_chain> m_chains;
    std::vector<segment> m_prev_start_segments;
    fastuidraw::BoundingBox<float> m_bounding_box;
  };

  class PartitionedTessellatedPathPrivate:fastuidraw::noncopyable
  {
  public:
    explicit
    PartitionedTessellatedPathPrivate(const fastuidraw::TessellatedPath &path);

    ~PartitionedTessellatedPathPrivate();

    bool m_has_arcs;
    SubsetPrivate *m_root_subset;
    std::vector<SubsetPrivate*> m_subsets;
  };
}

/////////////////////////////////////
// SubsetPrivate methods
SubsetPrivate*
SubsetPrivate::
create(const fastuidraw::TessellatedPath &P,
       std::vector<SubsetPrivate*> &out_values)
{
  ChainBuilder chain_builder;
  SubsetPrivate *root;

  for (unsigned int C = 0, endC = P.number_contours(); C < endC; ++C)
    {
      for (unsigned int E = 0, endE = P.number_edges(C); E < endE; ++E)
        {
          fastuidraw::c_array<const fastuidraw::TessellatedPath::segment> segs;

          chain_builder.start_chain(nullptr);
          segs = P.edge_segment_data(C, E);
          for (const fastuidraw::TessellatedPath::segment &S : segs)
            {
              chain_builder.add_segment(S);
            }
        }
    }

  root = FASTUIDRAWnew SubsetPrivate(0, chain_builder, out_values);
  return root;
}

SubsetPrivate::
SubsetPrivate(int recursion_depth, ChainBuilder &chains,
              std::vector<SubsetPrivate*> &out_values):
  m_ID(out_values.size()),
  m_children(nullptr, nullptr),
  m_bounding_box(chains.m_bbox)
{
  out_values.push_back(this);
  m_segments.swap(chains.m_segments);

  /* reserve before hand so that addresses of
   * values in m_prev_start_segments do not
   * change.
   */
  m_chains.reserve(chains.m_chains.size());
  m_prev_start_segments.reserve(chains.m_chains.size());

  fastuidraw::c_array<const segment> segs(fastuidraw::make_c_array(m_segments));
  for (const ChainBuilder::PerChain &R : chains.m_chains)
    {
      fastuidraw::c_array<const segment> sub_segs;
      sub_segs = segs.sub_array(R.m_range);
      if (!sub_segs.empty())
        {
          segment_chain V;

          if (R.m_has_before_first)
            {
              m_prev_start_segments.push_back(R.m_before_first);
              V.m_prev_to_start = &m_prev_start_segments.back();
            }
          else
            {
              V.m_prev_to_start = nullptr;
            }
          V.m_segments = sub_segs;
          m_chains.push_back(V);
        }
    }

  if (m_segments.size() >= splitting_threshhold
      && recursion_depth <= max_recursion_depth)
    {
      make_children(recursion_depth, out_values);
    }
}

SubsetPrivate::
~SubsetPrivate()
{
  if (m_children[0])
    {
      FASTUIDRAWassert(m_children[1]);

      FASTUIDRAWdelete(m_children[0]);
      FASTUIDRAWdelete(m_children[1]);
    }
}

void
SubsetPrivate::
process_chain(int splitting_coordinate, float splitting_value,
              const segment_chain &chain,
              ChainBuilder *before_chain, ChainBuilder *after_chain)
{
  using namespace fastuidraw;

  ChainBuilder *last_added_to(nullptr);
  const segment *prev_segment(chain.m_prev_to_start);

  for (const segment &S : chain.m_segments)
    {
      enum TessellatedPath::split_t split;
      segment before, after;

      split = S.compute_split(splitting_value,
                              &before, &after,
                              splitting_coordinate);

      switch (split)
        {
        case TessellatedPath::segment_completey_before_split:
          {
            if (last_added_to != before_chain)
              {
                before_chain->start_chain(prev_segment);
                last_added_to = before_chain;
              }
            prev_segment = before_chain->add_segment(S);
          }
          break;

        case TessellatedPath::segment_completey_after_split:
          {
            if (last_added_to != after_chain)
              {
                after_chain->start_chain(prev_segment);
                last_added_to = after_chain;
              }
            prev_segment = after_chain->add_segment(S);
          }
          break;

        case TessellatedPath::segment_split_start_before:
          {
            if (last_added_to != before_chain)
              {
                before_chain->start_chain(prev_segment);
              }
            prev_segment = before_chain->add_segment(before);
            after_chain->start_chain(prev_segment);
            prev_segment = after_chain->add_segment(after);
            last_added_to = after_chain;
          }
          break;

        case TessellatedPath::segment_split_start_after:
          {
            if (last_added_to != after_chain)
              {
                after_chain->start_chain(prev_segment);
              }
            prev_segment = after_chain->add_segment(after);
            before_chain->start_chain(prev_segment);
            prev_segment = before_chain->add_segment(before);
            last_added_to = before_chain;
          }
          break;
        } // switch

    } //for (const segment &S : chain)
}

int
SubsetPrivate::
choose_splitting_coordinate(float &split_value)
{
  using namespace fastuidraw;

  vecN<float, 2> split_values;
  vecN<unsigned int, 2> split_counters(0, 0);
  vecN<uvec2, 2> child_counters(uvec2(0, 0), uvec2(0, 0));
  std::vector<float> qs;
  unsigned int data_size(m_segments.size());

  qs.reserve(m_segments.size());
  /* For the most balanced split, we take the median
   * of the start and end points of the segments.
   */
  for (int c = 0; c < 2; ++c)
    {
      qs.clear();
      qs.push_back(m_bounding_box.min_point()[c]);
      qs.push_back(m_bounding_box.max_point()[c]);
      for(const segment &seg : m_segments)
        {
          qs.push_back(seg.m_start_pt[c]);
        }
      qs.push_back(m_segments.back().m_end_pt[c]);
      std::sort(qs.begin(), qs.end());
      split_values[c] = qs[qs.size() / 2];

      for(const segment &seg : m_segments)
        {
          bool sA, sB;
          sA = (seg.m_start_pt[c] < split_values[c]);
          sB = (seg.m_end_pt[c] < split_values[c]);
          if (sA != sB)
            {
              ++split_counters[c];
              ++child_counters[c][0];
              ++child_counters[c][1];
            }
          else
            {
              ++child_counters[c][sA];
            }
        }
    }

  /* choose the coordiante that splits the smallest number of edges */
  int canidate;
  canidate = (split_counters[0] < split_counters[1]) ? 0 : 1;

  /* we require that both sides will have fewer edges
   * than the parent size.
   */
  if (child_counters[canidate][0] < data_size
      && child_counters[canidate][1] < data_size)
    {
      split_value = split_values[canidate];
      return canidate;
    }
  else
    {
      return -1;
    }
}

void
SubsetPrivate::
make_children(int recursion_depth, std::vector<SubsetPrivate*> &out_values)
{
  /* step 1: find a good splitting coordinate and splitting value */
  int splitting_coordinate(-1);
  float splitting_value;

  splitting_coordinate = choose_splitting_coordinate(splitting_value);
  if (splitting_coordinate == -1)
    {
      return;
    }

  /* step 2: split each chain */
  ChainBuilder before_chain, after_chain;

  for (const auto &chain : m_chains)
    {
      process_chain(splitting_coordinate, splitting_value, chain,
                    &before_chain, &after_chain);
    }

  m_children[0]= FASTUIDRAWnew SubsetPrivate(recursion_depth + 1, before_chain, out_values);
  m_children[1]= FASTUIDRAWnew SubsetPrivate(recursion_depth + 1, after_chain, out_values);
}

unsigned int
SubsetPrivate::
select_subsets(ScratchSpacePrivate &scratch,
               fastuidraw::c_array<const fastuidraw::vec3> clip_equations,
               const fastuidraw::float3x3 &clip_matrix_local,
               const fastuidraw::vec2 &one_pixel_width,
               float pixels_additional_room,
               float item_space_additional_room,
               fastuidraw::c_array<unsigned int> dst)
{
  using namespace fastuidraw;

  scratch.m_adjusted_clip_eqs.resize(clip_equations.size());
  for(unsigned int i = 0; i < clip_equations.size(); ++i)
    {
      vec3 c(clip_equations[i]);
      float f;

      /* make "w" larger by the named number of pixels. */
      f = t_abs(c.x()) * one_pixel_width.x()
        + t_abs(c.y()) * one_pixel_width.y();

      c.z() += pixels_additional_room * f;

      /* transform clip equations from clip coordinates to
       * local coordinates.
       */
      scratch.m_adjusted_clip_eqs[i] = c * clip_matrix_local;
    }

  unsigned int return_value(0);
  select_subsets_implement(scratch, dst,
                           item_space_additional_room,
                           return_value);

  return return_value;
}

bool
SubsetPrivate::
select_subsets_implement(ScratchSpacePrivate &scratch,
                         fastuidraw::c_array<unsigned int> dst,
                         float item_space_additional_room,
                         unsigned int &current)
{
  using namespace fastuidraw;

  if (m_bounding_box.empty())
    {
      return false;
    }

  /* clip the bounding box of this StrokedPathSubset */
  vecN<vec2, 4> bb;
  bool unclipped;

  m_bounding_box.inflated_polygon(bb, item_space_additional_room);
  unclipped = detail::clip_against_planes(make_c_array(scratch.m_adjusted_clip_eqs),
                                          bb, &scratch.m_clipped_rect,
                                          scratch.m_clip_scratch_vec2s);

  //completely clipped
  if (scratch.m_clipped_rect.empty())
    {
      return false;
    }

  //completely unclipped.
  if (unclipped || !has_children())
    {
      dst[current] = m_ID;
      ++current;
      return true;
    }

  FASTUIDRAWassert(m_children[0] != nullptr);
  FASTUIDRAWassert(m_children[1] != nullptr);

  bool r0, r1;

  r0 = m_children[0]->select_subsets_implement(scratch, dst, item_space_additional_room, current);
  r1 = m_children[1]->select_subsets_implement(scratch, dst, item_space_additional_room, current);

  if (r0 && r1)
    {
      /* the last two elements added are m_children[0] and m_children[1];
       * remove them and add this instead (if possible).
       */
      FASTUIDRAWassert(current >= 2);
      FASTUIDRAWassert(dst[current - 2] == m_children[0]->m_ID);
      FASTUIDRAWassert(dst[current - 1] == m_children[1]->m_ID);

      current -= 2;
      dst[current] = m_ID;
      ++current;
      return true;
    }

  return false;
}

////////////////////////////////////////////
// PartitionedTessellatedPathPrivate methods
PartitionedTessellatedPathPrivate::
PartitionedTessellatedPathPrivate(const fastuidraw::TessellatedPath &path):
  m_has_arcs(path.has_arcs())
{
  m_root_subset = SubsetPrivate::create(path, m_subsets);
}

PartitionedTessellatedPathPrivate::
~PartitionedTessellatedPathPrivate()
{
  FASTUIDRAWdelete(m_root_subset);
}

/////////////////////////////////////////////////////////////////
// fastuidraw::PartitionedTessellatedPath::ScratchSpace methods
fastuidraw::PartitionedTessellatedPath::ScratchSpace::
ScratchSpace(void)
{
  m_d = FASTUIDRAWnew ScratchSpacePrivate();
}

fastuidraw::PartitionedTessellatedPath::ScratchSpace::
~ScratchSpace(void)
{
  ScratchSpacePrivate *d;
  d = static_cast<ScratchSpacePrivate*>(m_d);
  FASTUIDRAWdelete(d);
  m_d = nullptr;
}

/////////////////////////////////
// fastuidraw::PartitionedTessellatedPath::Subset methods
fastuidraw::PartitionedTessellatedPath::Subset::
Subset(void *d):
  m_d(d)
{
}

const fastuidraw::Rect&
fastuidraw::PartitionedTessellatedPath::Subset::
bounding_box(void) const
{
  SubsetPrivate *d;
  d = static_cast<SubsetPrivate*>(m_d);
  FASTUIDRAWassert(d);
  return d->bounding_box();
}

fastuidraw::c_array<const fastuidraw::PartitionedTessellatedPath::segment_chain>
fastuidraw::PartitionedTessellatedPath::Subset::
segment_chains(void) const
{
  SubsetPrivate *d;
  d = static_cast<SubsetPrivate*>(m_d);
  FASTUIDRAWassert(d);
  return d->segment_chains();
}

bool
fastuidraw::PartitionedTessellatedPath::Subset::
has_children(void) const
{
  SubsetPrivate *d;
  d = static_cast<SubsetPrivate*>(m_d);
  FASTUIDRAWassert(d);
  return d->has_children();
}

unsigned int
fastuidraw::PartitionedTessellatedPath::Subset::
ID(void) const
{
  SubsetPrivate *d;
  d = static_cast<SubsetPrivate*>(m_d);
  FASTUIDRAWassert(d);
  return d->ID();
}

fastuidraw::vecN<fastuidraw::PartitionedTessellatedPath::Subset, 2>
fastuidraw::PartitionedTessellatedPath::Subset::
children(void) const
{
  SubsetPrivate *d, *p0(nullptr), *p1(nullptr);

  d = static_cast<SubsetPrivate*>(m_d);
  if (d && d->has_children())
    {
      p0 = d->child(0);
      p1 = d->child(1);
    }

  Subset s0(p0), s1(p1);
  return vecN<Subset, 2>(s0, s1);
}

////////////////////////////////////////////
// fastuidraw::PartitionedTessellatedPath methods
fastuidraw::PartitionedTessellatedPath::
PartitionedTessellatedPath(const TessellatedPath &path)
{
  m_d = FASTUIDRAWnew PartitionedTessellatedPathPrivate(path);
}

fastuidraw::PartitionedTessellatedPath::
~PartitionedTessellatedPath()
{
  PartitionedTessellatedPathPrivate *d;
  d = static_cast<PartitionedTessellatedPathPrivate*>(m_d);
  FASTUIDRAWdelete(d);
}

bool
fastuidraw::PartitionedTessellatedPath::
has_arcs(void) const
{
  PartitionedTessellatedPathPrivate *d;
  d = static_cast<PartitionedTessellatedPathPrivate*>(m_d);
  return d->m_has_arcs;
}

unsigned int
fastuidraw::PartitionedTessellatedPath::
number_subsets(void) const
{
  PartitionedTessellatedPathPrivate *d;
  d = static_cast<PartitionedTessellatedPathPrivate*>(m_d);
  return d->m_subsets.size();
}

fastuidraw::PartitionedTessellatedPath::Subset
fastuidraw::PartitionedTessellatedPath::
subset(unsigned int I) const
{
  PartitionedTessellatedPathPrivate *d;
  d = static_cast<PartitionedTessellatedPathPrivate*>(m_d);

  FASTUIDRAWassert(I < d->m_subsets.size());
  return Subset(d->m_subsets[I]);
}

fastuidraw::PartitionedTessellatedPath::Subset
fastuidraw::PartitionedTessellatedPath::
root_subset(void) const
{
  PartitionedTessellatedPathPrivate *d;
  d = static_cast<PartitionedTessellatedPathPrivate*>(m_d);
  return Subset(d->m_root_subset);
}

unsigned int
fastuidraw::PartitionedTessellatedPath::
select_subsets(ScratchSpace &scratch_space,
               c_array<const vec3> clip_equations,
               const float3x3 &clip_matrix_local,
               const vec2 &one_pixel_width,
               c_array<const float> geometry_inflation,
               c_array<unsigned int> dst) const
{
  PartitionedTessellatedPathPrivate *d;
  d = static_cast<PartitionedTessellatedPathPrivate*>(m_d);

  ScratchSpacePrivate *scratch_d;
  scratch_d = static_cast<ScratchSpacePrivate*>(scratch_space.m_d);

  FASTUIDRAWassert(dst.size() >= d->m_subsets.size());
  return d->m_root_subset->select_subsets(*scratch_d,
                                          clip_equations,
                                          clip_matrix_local,
                                          one_pixel_width,
                                          geometry_inflation[PathEnums::pixel_space_distance],
                                          geometry_inflation[PathEnums::item_space_distance],
                                          dst);
}