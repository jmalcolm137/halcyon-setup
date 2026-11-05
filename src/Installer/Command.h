/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Vector.h>

namespace Halcyon {

struct CommandResult {
    int exit_code { -1 };
    bool timed_out { false };
    ByteString output;
    ByteString error;

    bool ok() const { return exit_code == 0 && !timed_out; }
    ByteString combined_output() const
    {
        if (error.is_empty())
            return output;
        if (output.is_empty())
            return error;
        return ByteString::formatted("{}\n{}", output, error);
    }
};

bool command_exists(StringView program);
ByteString resolve_command(StringView program);

// Run a program without a shell. Standard input is optional and is closed
// after it has been written. A timeout of zero means "no timeout".
CommandResult run_command(ByteString const& program, Vector<ByteString> const& arguments = {}, ByteString const& input = {}, int timeout_seconds = 0);

}
