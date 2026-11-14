#pragma once

/** INTERSECT remote-control protocol: OSC over UDP on 127.0.0.1.
    The full specification lives in docs/remote-control-protocol.md; keep the two in sync.

    Versioning: kVersion bumps only on incompatible changes. New addresses and new trailing
    arguments are compatible additions, so clients must ignore what they do not understand.
*/
namespace RemoteProtocol
{
constexpr int kVersion = 1;

// Servers try these ports first so sandboxed clients can find an instance without the
// discovery directory, then fall back to an OS-assigned port.
constexpr int kFirstFixedPort = 9871;
constexpr int kNumFixedPorts = 8;

constexpr int kClientTimeoutMs = 6000;      // clients ping at least every 2 s
constexpr int kBoundsCommitIdleMs = 250;    // a slice-bounds gesture commits after this idle time
constexpr int kStateTimerHz = 30;
constexpr int kMaxPeakWidth = 4096;
constexpr int kMaxClients = 8;
}
