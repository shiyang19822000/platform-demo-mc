/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_AsyncPanZoomController_h
#define mozilla_layers_AsyncPanZoomController_h

#include "GeckoContentController.h"
#include "mozilla/ReentrantMonitor.h"
#include "mozilla/RefPtr.h"
#include "InputEvent.h"
#include "Axis.h"
#include "CompositeEvent.h"

namespace mozilla {
namespace layers {

class CompositorParent;

/**
 * Controller for all panning and zooming logic. Any time a user input is
 * detected and it must be processed in some way to affect what the user sees,
 * it goes through here. Listens for nsTouchEvent, nsTapEvent, nsPinchEvent and
 * mutates the viewport. Note that this is completely cross-platform.
 *
 * Input events, on Android, originate in TouchEventHandler and get sent
 * through the JNI and AndroidBridge into this class. They are handled
 * immediately. Usually handling these events involves panning or zooming the
 * screen.
 *
 * The compositor interacts with this class by locking it and querying it for
 * the current transform matrix based on the panning and zooming logic that was
 * invoked on the Java UI thread.
 */
class AsyncPanZoomController {
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AsyncPanZoomController)

  typedef mozilla::ReentrantMonitor ReentrantMonitor;

  AsyncPanZoomController(GeckoContentController* aController);
  ~AsyncPanZoomController();

  // --------------------------------------------------------------------------
  // These methods must only be called on the controller/UI thread.
  //

  /**
   * General handler for any input event. Calls another Handle*Event based on
   * the type of input.
   */
  nsEventStatus HandleInputEvent(const InputEvent& event);

  /**
   * Updates the viewport size, i.e. the dimensions of the screen content will
   * actually be rendered onto in device pixels.
   * The monitor must be held while calling this.
   */
  void UpdateViewportSize(int width, int height);

  // --------------------------------------------------------------------------
  // These methods must only be called on the compositor thread.
  //

  /**
   * The compositor is about to draw pannable/zoomable content. Get its current
   * transform per current animation state.
   */
  void GetContentTransformForFrame(const FrameMetrics& aFrame,
                                   const gfx3DMatrix& aRootTransform,
                                   const gfxSize& aWidgetSize,
                                   gfx3DMatrix* aTransform,
                                   gfxPoint* aReverseViewTranslation);

  /**
   * A shadow layer update has arrived. |aViewportFrame| is the new FrameMetrics
   * for the top-level frame.
   */
  void NotifyLayersUpdated(const FrameMetrics& aViewportFrame);

  /**
   * The nsWindow implementation for each platform must set the compositor
   * parent of the layer controller so that it can request composites.
   */
  void SetCompositorParent(CompositorParent* aCompositorParent);

  /**
   * Advances a fling one frame. This should be called as part of a fling
   * runnable only.
   */
  void DoFling();

  /**
   * Toggles the compositing flag. This is used for B2G where content processes
   * have been spawned but aren't actually being composited yet. This prevents
   * us from accidentally moving stuff around even though it's not active in the
   * layer tree.
   *
   * *** You must hold the monitor while calling this!
   */
  void SetCompositing(bool aCompositing);

  // --------------------------------------------------------------------------
  // These methods can be called from any thread.
  //

  /**
   * Check whether or not the metrics have been updated since a reset has last
   * been done.
   */
  bool GetMetricsUpdated();

  /**
   * Resets the metrics update status to false. This should be used once a
   * metrics update has been handled.
   */
  void ResetMetricsUpdated();

  /**
   * Gets the reentrant monitor for thread safety.
   */
  ReentrantMonitor& GetReentrantMonitor();

  /**
   * Gets the current frame metrics. This is *not* the Gecko copy stored in the
   * layers code.
   *
   * *** You must hold the monitor while calling this!
   */
  const FrameMetrics& GetFrameMetrics();

  /**
   * Sets the current frame metrics. This does *not* set the Gecko copy stored
   * in the layers code. To update that, we must send a message to browser.js
   * requesting a repaint with new metrics.
   *
   * *** You must hold the monitor while calling this!
   */
  void SetFrameMetrics(const FrameMetrics& aFrameMetrics);

  /**
   * Converts a point from layer view coordinates to layer coordinates. In other
   * words, given a point measured in pixels from the top left corner of the
   * layer view, returns the point in pixels measured from the last scroll.
   */
  const nsIntPoint ConvertViewPointToLayerPoint(const nsIntPoint& viewPoint);

  /**
   * Sets the DPI of the device for use within panning and zooming logic.
   *
   * *** You must hold the monitor while calling this! If you call this
   * immediately after initializing the class, it's safe to do so without
   * holding the monitor.
   */
  void SetDPI(int aDPI);

protected:
  /**
   * Message name for a viewport update, sent from browser.js. Sent when the
   * scroll position of the document changes.
   */
  static const char* VIEWPORT_UPDATE;

  /**
   * Message name for a page size update, sent from browser.js. Sent when the
   * page size changes in Gecko.
   */
  static const char* VIEWPORT_PAGE_SIZE;

  /**
   * Message name for a viewport update, sent from browser.js. Sent when a page
   * size or viewport update happens but this page is in the background.
   */
  static const char* VIEWPORT_CALCULATE_DISPLAY_PORT;

  /**
   * Epsilon helper for float precision.
   */
  static float EPSILON;

  /**
   * Maximum amount of time while panning before sending a viewport change. This
   * will asynchronously repaint the page. It is also forced when panning stops.
   */
  static PRInt32 REPAINT_INTERVAL;

  /**
   * Helper method for touches beginning. Sets everything up for panning and any
   * multitouch gestures.
   */
  nsEventStatus OnTouchStart(const MultiTouchEvent& event);

  /**
   * Helper method for touches moving. Does any transforms needed when panning.
   */
  nsEventStatus OnTouchMove(const MultiTouchEvent& event);

  /**
   * Helper method for touches ending. Redraws the screen if necessary and does
   * any cleanup after a touch has ended.
   */
  nsEventStatus OnTouchEnd(const MultiTouchEvent& event);

  /**
   * Helper method for touches being cancelled. Treated roughly the same as a
   * touch ending (OnTouchEnd()).
   */
  nsEventStatus OnTouchCancel(const MultiTouchEvent& event);

  /**
   * Helper method for scales beginning. Distinct from the OnTouch* handlers in
   * that this implies some outside implementation has determined that the user
   * is pinching. On Android, this is the SimpleScaleGestureDetector.
   */
  nsEventStatus OnScaleBegin(const PinchEvent& event);

  /**
   * Helper method for scaling. As the user moves their fingers when pinching,
   * this changes the scale of the page.
   */
  nsEventStatus OnScale(const PinchEvent& event);

  /**
   * Helper method for scales ending. Redraws the screen if necessary and does
   * any cleanup after a scale has ended.
   */
  nsEventStatus OnScaleEnd(const PinchEvent& event);

  /**
   * Wrapper for the GeckoContentController's SendGestureEvent method to first
   * take a message name and event, and transform the point to layer coordinates.
   */
  void SendGestureEvent(const TapEvent& event, const nsAString& message);

  /**
   * Helper method for long press gestures. Sends a notification to browser.js
   * if the press is valid.
   */
  nsEventStatus OnLongPress(const TapEvent& event);

  /**
   * Helper method for single tap gestures. Gets called before an
   * OnSingleTapConfirmed() call. Sends a notification to browser.js if the
   * tap is valid.
   */
  nsEventStatus OnSingleTapUp(const TapEvent& event);

  /**
   * Helper method for a single tap confirmed. Gets sent if a double tap doesn't
   * happen after a single tap within some outside implementation's timeout.
   * Sends a notification to browser.js if the tap is valid.
   */
  nsEventStatus OnSingleTapConfirmed(const TapEvent& event);

  /**
   * Helper method for double taps. Sends a notification to browser.js if the
   * tap is valid.
   */
  nsEventStatus OnDoubleTap(const TapEvent& event);

  /**
   * Helper method to cancel any gesture currently going to Gecko. Used
   * primarily when a user taps the screen over some clickable content but then
   * pans down instead of letting go (i.e. to cancel a previous touch so that a
   * new one can properly take effect.
   */
  nsEventStatus OnCancelTap(const TapEvent& event);

  /**
   * Scrolls the viewport by an X,Y offset.
   * The monitor must be held while calling this.
   */
  void ScrollBy(const nsIntPoint& aOffset);

  /**
   * Scales the viewport by an amount (note that it doesn't set the scale to the
   * inputted value but multiplies it in). Also considers a focus point so that
   * the page zooms outward from that point.
   * The monitor must be held while calling this.
   */
  void ScaleWithFocus(float aScale, const nsIntPoint& aFocus);

  /**
   * Notifies the platform-specific code that it should redraw the page. All of
   * the viewport metrics must be set before calling this.
   * The monitor must be held while calling this.
   */
  void ForceRepaint();

  /**
   * XXX: Do we want this?
   * Bounces the screen back to viewport metrics that don't overscroll.
   */
  void MaybeBounce();

  /**
   * Cancels any currently running operation. Note that this should be called in
   * conjunction with changing mState, otherwise any runnable currently running
   * will still run.
   */
  void CancelAnimation();

  /**
   * Gets the displacement that has been travelled by a single touch since it
   * began. That is, it is the distance between the current position and the
   * initial position of this touch.
   */
  float PanDistance(const MultiTouchEvent& event);

  /**
   * Gets a vector of the velocities of each axis.
   */
  const nsPoint GetVelocityVector();

  /**
   * Gets a reference to the first SingleTouchData from an nsTouchEvent.
   * There is an array of these on every nsTouchEvent that is used off-main-thread.
   * This gets only the first one and assumes the rest are either missing or not relevant.
   */
  SingleTouchData& GetTouchFromEvent(const MultiTouchEvent& event);

  /**
   * Does any panning required due to a new touch event.
   */
  void TrackTouch(const MultiTouchEvent& event);

  /**
   * Recalculates the displayport. Ideally, this should paint an area bigger
   * than the actual screen. The viewport refers to the size of the screen,
   * while the displayport is the area actually painted by Gecko. We paint
   * a larger area than the screen so that when you scroll down, you don't
   * checkerboard immediately.
   */
  const nsIntRect CalculatePendingDisplayPort();

  /**
   * Utility function to send an updated viewport. Calls into
   * GeckoContentController to do the actual work.
   */
  void SendViewportChange();

private:
  enum PanZoomState {
    NOTHING,        /* no touch-start events received */
    FLING,          /* all touches removed, but we're still scrolling page */
    TOUCHING,       /* one touch-start event received */
    PANNING_LOCKED, /* touch-start followed by move (i.e. panning with axis lock) */
    PANNING,        /* panning without axis lock */
    PANNING_HOLD,   /* in panning, but not moving.
                     * similar to TOUCHING but after starting a pan */
    PANNING_HOLD_LOCKED, /* like PANNING_HOLD, but axis lock still in effect */
    PINCHING,       /* nth touch-start, where n > 1. this mode allows pan and zoom */
    ANIMATED_ZOOM,  /* animated zoom to a new rect */
    BOUNCE,         /* in a bounce animation */

    WAITING_LISTENERS, /* a state halfway between NOTHING and TOUCHING - the user has
                    put a finger down, but we don't yet know if a touch listener has
                    prevented the default actions yet. we still need to abort animations. */
  };

  CompositorParent *mCompositorParent;
  PanZoomState mState;
  AxisX mX;
  AxisY mY;
  PRInt32 mLastEventTime;
  PRInt32 mLastRepaint;
  nsIntPoint mLastZoomFocus;
  FrameMetrics mFrameMetrics;
  bool mMetricsUpdated;
  bool mIsCompositing;
  mutable ReentrantMonitor mReentrantMonitor;
  int mDPI;

  int mPanThreshold;

  nsRefPtr<GeckoContentController> mGeckoContentController;

  friend class nsWindow;
};

}
}

#endif // mozilla_layers_PanZoomController_h
