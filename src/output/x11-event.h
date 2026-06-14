#ifndef CONKY_X11_EVENT_H
#define CONKY_X11_EVENT_H

#include "config.h"

#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

extern "C" {
#include <X11/Xlib.h>

#ifdef BUILD_XDAMAGE
#include <X11/extensions/Xdamage.h>
#endif /* BUILD_XDAMAGE */
}

#include "../mouse-events.h"

namespace conky::x11 {
enum class event_error {
  /// Cookie was already consumed when parsing was attempted.
  ALREADY_CONSUMED,
  /// Parser not implemented for runtime type.
  NOT_IMPLEMENTED,
  /// Unable to parse event data.
  PARSING_FAILURE,
  /// Explicitly set to none, as part of execution control.
  EXPLICIT_NONE,
};

struct event;

struct event {
  /// Variant type indices aren't stable, use `event_variant_index_of_v` if
  /// you need stable index for a specific event type.
  using variant = std::variant<
      event_error,

      XKeyEvent, XButtonEvent, XMotionEvent, XCrossingEvent, XFocusChangeEvent,
      XKeymapEvent, XExposeEvent, XGraphicsExposeEvent, XNoExposeEvent,
      XVisibilityEvent, XCreateWindowEvent, XDestroyWindowEvent, XUnmapEvent,
      XMapEvent, XMapRequestEvent, XReparentEvent, XConfigureEvent,
      XConfigureRequestEvent, XGravityEvent, XResizeRequestEvent,
      XCirculateEvent, XCirculateRequestEvent, XPropertyEvent,
      XSelectionClearEvent, XSelectionRequestEvent, XSelectionEvent,
      XColormapEvent, XClientMessageEvent, XMappingEvent,

#ifdef BUILD_XDAMAGE
      XDamageNotifyEvent,
#endif /* BUILD_XDAMAGE */

      xi_pointer_move, xi_pointer_press, xi_pointer_release,

      xi_pointer_enter, xi_pointer_leave, xi_pointer_focus_in,
      xi_pointer_focus_out>;

  variant inner;

 private:
  constexpr event(event_error err) : inner(err) {}
  event(Display *display, XEvent inner);

 public:
  /// Constructs an event directly wrapping any of the `variant`
  /// alternatives.
  ///
  /// `event_error` is intentionally excluded so error/none states can only
  /// be produced internally (see `event::none()`).
  template <
      typename Inner,
      std::enable_if_t<std::is_constructible_v<variant, Inner &&> &&
                           !std::is_same_v<std::decay_t<Inner>, event_error>,
                       int> = 0>
  constexpr event(Inner &&inner) : inner(std::forward<Inner>(inner)) {}
  constexpr event() : inner(event_error::EXPLICIT_NONE) {}

  template <
      typename Inner,
      std::enable_if_t<std::is_constructible_v<variant, Inner &&> &&
                           !std::is_same_v<std::decay_t<Inner>, event_error>,
                       int> = 0>
  constexpr event &operator=(Inner &&value) {
    inner = std::forward<Inner>(value);
    return *this;
  }

  static inline event read(Display *display) {
    XEvent ev;
    XNextEvent(display, &ev);
    return event(display, ev);
  }

  constexpr std::optional<event_error> as_err() const noexcept {
    if (is_err()) { return std::get<event_error>(inner); }
    return std::nullopt;
  }
  constexpr bool is_err() const noexcept {
    return std::holds_alternative<event_error>(inner) &&
           std::get<event_error>(inner) != event_error::EXPLICIT_NONE;
  }
  constexpr bool is_none() const noexcept {
    return std::holds_alternative<event_error>(inner) &&
           std::get<event_error>(inner) == event_error::EXPLICIT_NONE;
  }
  constexpr bool is_some() const noexcept {
    return !std::holds_alternative<event_error>(inner);
  }

  int raw_x11_type() const;

  /// Allows checking if the `window` is the one that subscribed to this
  /// event.
  bool is_window_listener(Window window) const;

  /// Answers the question whether `window` argument is the one that is
  /// actually affected by the event.
  ///
  /// Examples:
  /// - In `XKeyEvent`, the child (if any) is the subject
  /// - In `XSelectionEvent` and `XSelectionRequestEvent`, `requestor` is the
  ///   subject
  ///
  /// This function will not work on window hierarchy >2.
  bool is_window_subject(Window window) const;

  template <typename T,
            std::enable_if_t<!std::is_same_v<T, event_error>, int> = 0>
  std::optional<std::reference_wrapper<T>> downcast();

  template <typename T,
            std::enable_if_t<!std::is_same_v<T, event_error>, int> = 0>
  inline std::optional<std::reference_wrapper<const T>> downcast() const {
    return const_cast<event &>(*this).downcast<T>();
  }

  template <typename T>
  std::optional<T> into_inner() {
    if (!std::holds_alternative<T>(inner)) { return std::nullopt; }
    variant result = event_error::ALREADY_CONSUMED;
    std::swap(inner, result);
    return std::move(std::get<T>(result));
  }

  constexpr std::size_t variant_index() const { return inner.index(); }
};

namespace detail {
template <typename T, typename Variant, std::size_t... I>
constexpr std::size_t find_variant_index(std::index_sequence<I...>) {
  std::size_t result = sizeof...(I);
  ((std::is_same_v<T, std::variant_alternative_t<I, Variant>> ? (result = I)
                                                              : result),
   ...);
  return result;
}
}  // namespace detail

template <typename T>
constexpr std::size_t event_variant_index_of_v =
    detail::find_variant_index<T, event::variant>(
        std::make_index_sequence<std::variant_size_v<event::variant>>{});

}  // namespace conky::x11

#endif
