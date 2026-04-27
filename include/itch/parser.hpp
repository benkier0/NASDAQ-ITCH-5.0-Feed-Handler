#pragma once
#include "messages.hpp"
#include <cstdint>
#include <cstddef>

namespace itch {

template<typename Handler>
concept MessageHandler = requires(Handler& h,
    const AddOrder&               a,
    const AddOrderMPID&           f,
    const OrderExecuted&          e,
    const OrderExecutedWithPrice& c,
    const OrderCancel&            x,
    const OrderDelete&            d,
    const OrderReplace&           u,
    const SystemEvent&            s)
{
    h.on_add_order(a);
    h.on_add_order_mpid(f);
    h.on_order_executed(e);
    h.on_order_executed_with_price(c);
    h.on_order_cancel(x);
    h.on_order_delete(d);
    h.on_order_replace(u);
    h.on_system_event(s);
};

template<MessageHandler Handler>
[[nodiscard]] bool dispatch(const uint8_t* buf, uint16_t len, Handler& h) noexcept {
    if (__builtin_expect(len == 0, 0)) return false;

    switch (buf[0]) {
    case MSG_ADD_ORDER:
        if (__builtin_expect(len < sizeof(AddOrder), 0)) return false;
        h.on_add_order(*reinterpret_cast<const AddOrder*>(buf));
        return true;

    case MSG_ADD_ORDER_MPID:
        if (__builtin_expect(len < sizeof(AddOrderMPID), 0)) return false;
        h.on_add_order_mpid(*reinterpret_cast<const AddOrderMPID*>(buf));
        return true;

    case MSG_ORDER_EXECUTED:
        if (__builtin_expect(len < sizeof(OrderExecuted), 0)) return false;
        h.on_order_executed(*reinterpret_cast<const OrderExecuted*>(buf));
        return true;

    case MSG_ORDER_EXEC_PRICE:
        if (__builtin_expect(len < sizeof(OrderExecutedWithPrice), 0)) return false;
        h.on_order_executed_with_price(*reinterpret_cast<const OrderExecutedWithPrice*>(buf));
        return true;

    case MSG_ORDER_CANCEL:
        if (__builtin_expect(len < sizeof(OrderCancel), 0)) return false;
        h.on_order_cancel(*reinterpret_cast<const OrderCancel*>(buf));
        return true;

    case MSG_ORDER_DELETE:
        if (__builtin_expect(len < sizeof(OrderDelete), 0)) return false;
        h.on_order_delete(*reinterpret_cast<const OrderDelete*>(buf));
        return true;

    case MSG_ORDER_REPLACE:
        if (__builtin_expect(len < sizeof(OrderReplace), 0)) return false;
        h.on_order_replace(*reinterpret_cast<const OrderReplace*>(buf));
        return true;

    case MSG_SYSTEM_EVENT:
        if (__builtin_expect(len < sizeof(SystemEvent), 0)) return false;
        h.on_system_event(*reinterpret_cast<const SystemEvent*>(buf));
        return true;

    default:
        return true;
    }
}

} // namespace itch
