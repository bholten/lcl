#!/bin/sh
# fake-router.sh - a small interactive "device console" for the
# lcl-expect guide (packages/lcl-expect/docs/Expect.lcl).
#
# Login is admin / hunter2 with three attempts. The prompt accepts
# help, show version, show uptime, show log (paged with --More--),
# reboot (asks for confirmation) and exit.

attempts=0

while :; do
    printf 'router-01 login: '
    read -r user || exit 0
    stty -echo 2>/dev/null
    printf 'Password: '
    read -r pass || exit 0
    stty echo 2>/dev/null
    printf '\n'
    if [ "$user" = admin ] && [ "$pass" = hunter2 ]; then
        break
    fi
    attempts=$((attempts + 1))
    if [ "$attempts" -ge 3 ]; then
        echo 'Too many failed logins.'
        exit 1
    fi
    echo 'Login incorrect'
done

echo 'Welcome to router-01. Type help for a list of commands.'

while :; do
    printf 'router-01> '
    read -r cmd || exit 0
    case "$cmd" in
        help)
            echo 'commands: show version, show uptime, show log, reboot, exit' ;;
        'show version')
            echo 'RouterOS 7.2.1 (build 4711)' ;;
        'show uptime')
            echo 'Uptime: 4 days, 3 hours, 17 minutes' ;;
        'show log')
            i=1
            while [ "$i" -le 25 ]; do
                printf '%02d link state change on eth%d\n' "$i" $((i % 4))
                if [ $((i % 10)) -eq 0 ]; then
                    printf -- '--More--'
                    read -r _ || exit 0
                fi
                i=$((i + 1))
            done ;;
        reboot)
            printf 'Reboot now? [y/N] '
            read -r answer || exit 0
            case "$answer" in
                y|Y) echo 'Rebooting...'; exit 0 ;;
                *) echo 'Cancelled.' ;;
            esac ;;
        exit|quit)
            echo 'Bye.'; exit 0 ;;
        '') ;;
        *)
            echo "Unknown command: $cmd" ;;
    esac
done
