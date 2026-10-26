#!/bin/sh
# test_protocol.sh - Automated tests for the segment B command channel.
#
# Run as root inside the VM, AFTER `insmod rootkit_b.ko`:
#     sh test_protocol.sh
#
# Each command is written and its response read on the SAME file descriptor
# (the response is staged per open file). Responses are compared byte for byte
# as hex strings. POSIX sh / busybox compatible.

DEV=/dev/companion
pass=0
fail=0

# send_recv "<printf-format-of-request-bytes>"
# Writes the request, reads the response, prints it as a bare hex string.
send_recv() {
	exec 3<>"$DEV"
	printf "$1" >&3
	dd bs=64 count=1 <&3 2>/dev/null | od -An -tx1 | tr -d ' \n'
	exec 3>&-
}

check() {
	# check "<description>" "<expected-hex>" "<actual-hex>"
	if [ "$2" = "$3" ]; then
		echo "PASS  $1"
		pass=$((pass + 1))
	else
		echo "FAIL  $1"
		echo "        attendu: $2"
		echo "        obtenu : $3"
		fail=$((fail + 1))
	fi
}

if [ ! -e "$DEV" ]; then
	echo "ERREUR: $DEV absent. As-tu fait 'insmod rootkit_b.ko' ?"
	exit 1
fi

echo "== Tests du protocole sur $DEV =="

# CMD_PING (0x01), arg_len=0  -> status=0, data_len=0
got=$(send_recv '\x01\x00\x00\x00\x00\x00\x00\x00')
check "CMD_PING -> OK, 0 octet" "0000000000000000" "$got"

# CMD_GET_VERSION (0x02), arg_len=0  -> status=0, data_len=5, "0.1.0"
got=$(send_recv '\x02\x00\x00\x00\x00\x00\x00\x00')
check "CMD_GET_VERSION -> OK + \"0.1.0\"" "0000000005000000302e312e30" "$got"

# CMD_SET_FLAG (0x03), arg_len=4, flag_id=3  -> status=0
got=$(send_recv '\x03\x00\x00\x00\x04\x00\x00\x00\x03\x00\x00\x00')
check "CMD_SET_FLAG(3) -> OK" "0000000000000000" "$got"

# CMD_CLEAR_FLAG (0x04), arg_len=4, flag_id=3  -> status=0
got=$(send_recv '\x04\x00\x00\x00\x04\x00\x00\x00\x03\x00\x00\x00')
check "CMD_CLEAR_FLAG(3) -> OK" "0000000000000000" "$got"

# CMD_SET_FLAG with out-of-range id (64)  -> status=1 (ERR)
got=$(send_recv '\x03\x00\x00\x00\x04\x00\x00\x00\x40\x00\x00\x00')
check "CMD_SET_FLAG(64) -> ERR" "0100000000000000" "$got"

# Unknown command (0xFF)  -> status=2 (UNKNOWN)
got=$(send_recv '\xff\x00\x00\x00\x00\x00\x00\x00')
check "commande inconnue -> UNKNOWN" "0200000000000000" "$got"

echo "== Resultat : $pass OK, $fail KO =="
[ "$fail" -eq 0 ]
