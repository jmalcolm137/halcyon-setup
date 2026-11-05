/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Config.h"
#include <AK/Function.h>

namespace Halcyon {

struct InstallCallbacks {
    Function<void(ByteString const&)> on_log;
    Function<void(int, ByteString const&)> on_progress;
    Function<bool()> is_cancelled;
};

struct InstallResult {
    bool success { false };
    ByteString message;
};

InstallResult run_install(InstallConfig const&, InstallCallbacks);

// Used by the headless test and by `--dump-plan`.
ByteString describe_plan(Vector<PlannedPartition> const&, bool uefi);

}
