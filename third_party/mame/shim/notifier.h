// SPDX-License-Identifier: GPL-3.0-or-later
// HIRAKU: stand-in for MAME's util/notifier.h (media-change notifications: discs never change here).
#pragma once
namespace util {
class notifier_subscription {
public:
    notifier_subscription() = default;
};
}  // namespace util
