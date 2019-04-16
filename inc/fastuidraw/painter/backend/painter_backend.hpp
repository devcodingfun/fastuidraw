/*!
 * \file painter_backend.hpp
 * \brief file painter_backend.hpp
 *
 * Copyright 2016 by Intel.
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


#pragma once

#include <fastuidraw/util/blend_mode.hpp>
#include <fastuidraw/util/rect.hpp>
#include <fastuidraw/text/glyph_atlas.hpp>
#include <fastuidraw/image.hpp>
#include <fastuidraw/colorstop_atlas.hpp>
#include <fastuidraw/painter/backend/painter_draw.hpp>
#include <fastuidraw/painter/backend/painter_shader_registrar.hpp>
#include <fastuidraw/painter/backend/painter_surface.hpp>


namespace fastuidraw
{
/*!\addtogroup PainterBackend
 * @{
 */

  /*!
   * \brief
   * A PainterBackend is an interface that defines the API-specific
   * elements to implement \ref Painter. A fixed PainterBackend will
   * only be used by a single \ref Painter because a \ref Painter
   * does NOT use the backend it is passed, instead it creates a
   * backend for its own private use via \ref create_shared().
   *
   * A \ref Painter will use a \ref PainterBackend as follows within a
   * Painter::begin() and Painter::end() pair.
   * \code
   * fastuidraw::PainterBackend &backend;
   *
   * backend.on_painter_begin();
   * for (how many surfaces S needed to draw all)
   *   {
   *     std::vector<fastuidraw::reference_counted_ptr<fastuidraw::PainterDraw> > draws;
   *     for (how many PainterDraw objects needed to draw what is drawn in S)
   *       {
   *         fastuidraw::reference_counted_ptr<fastuidraw::PainterDraw> p;
   *
   *         p = backend.map_draw();
   *         // fill the buffers on p, potentially calling
   *         // PainterDraw::draw_break() several times.
   *         p.get()->unmap(attributes_written, indices_written, data_store_written);
   *         draws.push_back(p);
   *       }
   *     backend.on_pre_draw(S, maybe_clear_color_buffer, maybe_begin_new_target);
   *     for (p in draws)
   *       {
   *         p.get()->draw();
   *       }
   *     draws.clear();
   *     backend.on_post_draw();
   *   }
   * \endcode
   */
  class PainterBackend:public reference_counted<PainterBackend>::concurrent
  {
  public:
    /*!
     * \brief
     * A ConfigurationBase holds how data should be set to a
     * PainterBackend
     */
    class ConfigurationBase
    {
    public:
      /*!
       * Ctor.
       */
      ConfigurationBase(void);

      /*!
       * Copy ctor.
       */
      ConfigurationBase(const ConfigurationBase &obj);

      ~ConfigurationBase();

      /*!
       * assignment operator
       */
      ConfigurationBase&
      operator=(const ConfigurationBase &obj);

      /*!
       * Swap operation
       * \param obj object with which to swap
       */
      void
      swap(ConfigurationBase &obj);

      /*!
       * If true, indicates that the PainterBackend supports
       * bindless texturing. Default value is false.
       */
      bool
      supports_bindless_texturing(void) const;

      /*!
       * Specify the return value to supports_bindless_texturing() const.
       * Default value is false.
       */
      ConfigurationBase&
      supports_bindless_texturing(bool);

    private:
      void *m_d;
    };

    /*!
     * \brief
     * PerformanceHints provides miscellaneous data about
     * an implementation of a PainterBackend.
     */
    class PerformanceHints
    {
    public:
      /*!
       * Ctor.
       */
      PerformanceHints(void);

      /*!
       * Copy ctor.
       */
      PerformanceHints(const PerformanceHints &obj);

      ~PerformanceHints();

      /*!
       * assignment operator
       */
      PerformanceHints&
      operator=(const PerformanceHints &obj);

      /*!
       * Swap operation
       * \param obj object with which to swap
       */
      void
      swap(PerformanceHints &obj);

      /*!
       * Returns true if an implementation of PainterBackend
       * clips triangles (for example by a hardware clipper
       * or geometry shading) instead of discard to implement
       * clipping as embodied by \ref PainterClipEquations.
       */
      bool
      clipping_via_hw_clip_planes(void) const;

      /*!
       * Set the value returned by
       * clipping_via_hw_clip_planes(void) const,
       * default value is true.
       */
      PerformanceHints&
      clipping_via_hw_clip_planes(bool v);

      /*!
       * Gives the maximum z-value an implementation of
       * PainterBackend support.
       */
      int
      max_z(void) const;

      /*!
       * Set the value returned by max_z(void) const,
       * default value is 2^20.
       */
      PerformanceHints&
      max_z(int);

    private:
      void *m_d;
    };

    /*!
     * Ctor.
     * \param glyph_atlas GlyphAtlas for glyphs drawn by the PainterBackend
     * \param image_atlas ImageAtlas for images drawn by the PainterBackend
     * \param colorstop_atlas ColorStopAtlas for color stop sequences drawn by the PainterBackend
     * \param shader_registrar PainterShaderRegistrar to which shaders are registered
     * \param config ConfigurationBase for how to pack data to PainterBackend
     * \param pdefault_shaders default shaders for PainterBackend; shaders are
     *                         registered at constructor.
     */
    PainterBackend(reference_counted_ptr<GlyphAtlas> glyph_atlas,
                   reference_counted_ptr<ImageAtlas> image_atlas,
                   reference_counted_ptr<ColorStopAtlas> colorstop_atlas,
                   reference_counted_ptr<PainterShaderRegistrar> shader_registrar,
                   const ConfigurationBase &config,
                   const PainterShaderSet &pdefault_shaders);

    virtual
    ~PainterBackend();

    /*!
     * To be implemented by a derived class to return
     * the number of attributes a PainterDraw returned
     * by map_draw() is guaranteed to hold.
     */
    virtual
    unsigned int
    attribs_per_mapping(void) const = 0;

    /*!
     * To be implemented by a derived class to return
     * the number of indices a PainterDraw returned
     * by map_draw() is guaranteed to hold.
     */
    virtual
    unsigned int
    indices_per_mapping(void) const = 0;

    /*!
     * Returns a handle to the GlyphAtlas of this
     * PainterBackend. All glyphs used by this
     * PainterBackend must live on glyph_atlas().
     */
    const reference_counted_ptr<GlyphAtlas>&
    glyph_atlas(void);

    /*!
     * Returns a handle to the ImageAtlas of this
     * PainterBackend. All images used by all brushes
     * of this PainterBackend must live on image_atlas().
     */
    const reference_counted_ptr<ImageAtlas>&
    image_atlas(void);

    /*!
     * Returns a handle to the ColorStopAtlas of this
     * PainterBackend. All color stops used by all brushes
     * of this PainterBackend must live on colorstop_atlas().
     */
    const reference_counted_ptr<ColorStopAtlas>&
    colorstop_atlas(void);

    /*!
     * Returns the PainterShaderRegistrar of this PainterBackend.
     * Use this return value to add custom shaders. NOTE: shaders
     * added within a thread are not useable within that thread
     * until the next call to begin().
     */
    const reference_counted_ptr<PainterShaderRegistrar>&
    painter_shader_registrar(void);

    /*!
     * Returns the ConfigurationBase passed in the ctor.
     */
    const ConfigurationBase&
    configuration_base(void) const;

    /*!
     * Called just before calling PainterDraw::draw() on a sequence
     * of PainterDraw objects who have had their PainterDraw::unmap()
     * routine called. An implementation will  will clear the depth
     * (aka occlusion) buffer and optionally the color buffer in the
     * viewport of the \ref PainterSurface.
     * \param surface the \ref PainterSurface to which to
     *                render content
     * \param clear_color_buffer if true, clear the color buffer
     *                           on the viewport of the surface.
     * \param begin_new_target if true indicates that drawing is to
     *                         start on the surface (typically this
     *                         means that when this is true that the
     *                         backend will clear all auxiliary buffers
     *                         (such as the depth buffer).
     */
    virtual
    void
    on_pre_draw(const reference_counted_ptr<PainterSurface> &surface,
                bool clear_color_buffer,
                bool begin_new_target) = 0;

    /*!
     * Called just after calling PainterDraw::draw()
     * on a sequence of PainterDraw objects.
     */
    virtual
    void
    on_post_draw(void) = 0;

    /*!
     * Called to return an action to bind an Image whose backing
     * store requires API binding.
     * \param slot which of the external image slots to bind the image to
     * \param im Image backed by a gfx API surface that in order to be used,
     *           must be bound. In patricular im's Image::type() value
     *           is Image::context_texture2d
     */
    virtual
    reference_counted_ptr<PainterDrawBreakAction>
    bind_image(unsigned int slot,
               const reference_counted_ptr<const Image> &im) = 0;

    /*!
     * Called to return an action to bind a \ref PainterSurface
     * to be used as the read from the deferred coverage buffer.
     * \param cvg_surface coverage surface backing the deferred
     *                    coverage buffer from which to read
     */
    virtual
    reference_counted_ptr<PainterDrawBreakAction>
    bind_coverage_surface(const reference_counted_ptr<PainterSurface> &cvg_surface) = 0;

    /*!
     * To be implemented by a derived class to return a PainterDraw
     * for filling of data.
     */
    virtual
    reference_counted_ptr<PainterDraw>
    map_draw(void) = 0;

    /*!
     * Returns the PainterShaderSet for the backend.
     * Returned values will already be registerd by the
     * backend.
     */
    const PainterShaderSet&
    default_shaders(void);

    /*!
     * Returns the PerformanceHints for the PainterBackend,
     * may only be called after on_begin() has been called
     * atleast once. The value returned is expected to stay
     * constant once on_begin() has been called.
     */
    const PerformanceHints&
    hints(void) const;

    /*!
     * To be implemented by a derived class to perform any caching
     * or other operations when \ref Painter has Painter::begin()
     * and to return the number of external texture slots the
     * PainterBackend supports.
     */
    virtual
    unsigned int
    on_painter_begin(void) = 0;

  protected:
    /*!
     * To be accessed by a derived class in its ctor
     * to set the performance hint values for itself.
     */
    PerformanceHints&
    set_hints(void);

  private:
    void *m_d;
  };
/*! @} */

}
