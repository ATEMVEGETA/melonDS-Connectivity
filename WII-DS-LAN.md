# Experimental Wii–DS LAN transport

Enable the existing Wii–DS connectivity setting and set `WII_DS_PEER_IPV4`
to the IPv4 address of the PC running a compatible LAN-enabled Dolphin build
before starting melonDS. Restart melonDS after changing it.

An unset or empty value retains the original loopback-only transport.
Malformed addresses fail closed. Numeric IPv4 unicast addresses only are
supported; hostnames and IPv6 are not resolved.

LAN mode listens on all IPv4 interfaces using the existing UDP port range
7065–7080, accepts discovery only from the configured Dolphin address, and
requires the selected Dolphin source port after discovery. Packet formats
and emulated wireless timing are unchanged. Dolphin must also have matching
LAN transport support and be configured with this melonDS device's address.

Use a trusted LAN: source filtering is not authentication or encryption.
Allow the programs through the private-network firewall; no external server
or router port forwarding is required. Test with copied game saves.

Related Dolphin implementation: https://github.com/ATEMVEGETA/Dolphin/pull/1
