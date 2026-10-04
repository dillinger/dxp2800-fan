# dxp2800-fan

Fan control for the **UGREEN NASync DXP2800** on **FreeBSD**.

The DXP2800's fan is driven by an ITE IT8613E Super I/O chip. FreeBSD has no
driver that can set its fan speed, so out of the box the fan runs on the
firmware's own curve. This project adds:

- **`itefan`**: a small tool that reads the chip's temperatures and fan speed
  and sets the fan to a fixed speed or back to automatic.
- **`fancurve`**: an `rc(8)` service that sets the fan speed from the CPU and
  disk temperatures, configured in `/etc/rc.conf`.

> **Warning.** This talks to the hardware directly. A fan left at a low fixed
> speed will not speed up when the machine gets hot. Read the safety notes
> below before using it.

## Requirements

- UGREEN NASync DXP2800 (Intel N100, ITE IT8613E). Other boards with an
  IT8613E may work; `itefan` refuses to run on other chips.
- FreeBSD on amd64, run as root (needs `/dev/io`).
- `coretemp(4)` for CPU temperatures (loaded automatically by the service).
- Optional: `sysutils/smartmontools` for disk temperatures.

## Install

### With the port (recommended)

The `port/` directory contains a FreeBSD port, `sysutils/dxp2800-fan`. With
poudriere, copy it into an overlay ports tree:

```sh
mkdir -p /usr/local/poudriere/overlay/sysutils
cp -R port/sysutils/dxp2800-fan /usr/local/poudriere/overlay/sysutils/
poudriere ports -c -p local -m null -M /usr/local/poudriere/overlay   # once
poudriere bulk -j <jail> -p default -O local sysutils/dxp2800-fan
pkg install dxp2800-fan
```

### By hand

```sh
make
make install          # installs into /usr/local
```

## Quick start

```sh
itefan                       # show temperatures, fan rpm, modes
itefan pct 3 25; sleep 5     # check that the fan still spins at 25 %
itefan                       # (look at the rpm on channel 3)

sysrc kld_list+=coretemp
sysrc fancurve_enable=YES
service fancurve start
```

## Using `itefan`

| Command | Effect |
|---|---|
| `itefan` | show chip, temperatures, rpm, mode and duty per channel |
| `itefan pct 3 40` | channel 3 to 40 % (manual mode) |
| `itefan set 3 200` | channel 3 to a raw duty of 200 (0 to 255) |
| `itefan auto 3` | channel 3 back to the chip's automatic mode |

On the DXP2800 the system fan is on **channel 3**. Stop the service before
setting the fan by hand (`service fancurve stop`), or it will override you
within 10 seconds.

## How `fancurve` reacts to temperature

Every 10 seconds it:

1. Reads the hottest CPU core and **averages it over the last 60 seconds**,
   so short spikes barely move the fan.
2. Turns the average into a target with the CPU curve
   (50 °C → 25 %, 80 °C → 100 %, linear in between).
   About once a minute it does the same for the hottest disk
   (38 °C → 25 %, 50 °C → 100 %); the higher target wins.
   Sleeping disks are never woken up.
3. Moves towards the target by at most **+10 %** or **−5 %** per round.
4. Goes to **100 % immediately** if a single CPU reading reaches 90 °C or the
   CPU temperature cannot be read.

When the service stops, the fan is handed back to the firmware.

## Configuration

All settings go in `/etc/rc.conf` (e.g. with `sysrc`), followed by
`service fancurve restart`. See `man fancurve` for details.

| Variable | Default | Meaning |
|---|---|---|
| `fancurve_enable` | `NO` | start at boot |
| `fancurve_channel` | `3` | PWM channel of the fan |
| `fancurve_interval` | `10` | seconds between readings |
| `fancurve_window` | `60` | seconds the CPU temperature is averaged over |
| `fancurve_min` | `25` | lowest fan speed, % |
| `fancurve_step_up` | `10` | max % faster per interval |
| `fancurve_step_down` | `5` | max % slower per interval |
| `fancurve_cpu` | `50:80` | CPU curve `lo:hi`, °C |
| `fancurve_crit` | `90` | instant 100 % at or above this CPU temperature |
| `fancurve_disks` | `auto` | disks to watch, `auto`, or empty for none |
| `fancurve_disk` | `38:50` | disk curve `lo:hi`, °C |
| `fancurve_verbose` | `NO` | log every reading (for tuning) |

Example, quieter and slower to react:

```sh
sysrc fancurve_window=120 fancurve_step_up=5 fancurve_min=30
service fancurve restart
```

Logs go to syslog (`grep fancurve /var/log/messages`).

## Safety notes

- Check that the fan still turns at `fancurve_min`; some fans stall at low
  duty cycles.
- `itefan` accesses the chip without locking. Don't run it alongside another
  driver or tool that uses the same chip.
- The firmware may reprogram the chip at boot or after suspend; `fancurve`
  rewrites its setting every interval.

## How it works

`itefan` enters the IT8613E's configuration mode on port `0x2e`, reads the
environment controller's base address (logical device 4), and then reads and
writes its registers. The register map follows the Linux `it87` hwmon driver.

## License

BSD 2-Clause, see [LICENSE](LICENSE).
