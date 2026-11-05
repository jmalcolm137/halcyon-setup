/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../Installer/Config.h"
#include <AK/Optional.h>
#include <LibGUI/Window.h>

namespace Halcyon {

Optional<UserAccount> show_add_user_dialog(GUI::Window&);
Optional<ByteString> show_mountpoint_dialog(GUI::Window&, ByteString const& partition, u64 size_bytes, ByteString const& current);
Optional<MountAssignment> show_format_dialog(GUI::Window&, MountAssignment const&);

}
