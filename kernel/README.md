# tpm-proxy

A Linux kernel driver to expose the TPM on an attached embedded Linux
device as a device on the host. This driver is used to provision the
TPM on the Xaptum router card.

## Supported Product IDs

| Description                              | USB Vendor ID | USB Product ID |
|------------------------------------------|---------------|----------------|
| Xaptum Router Card provisioning firmware | 0x2FE0        | 0x????         |


## Installation

### Debian (Stretch)

(TODO)

``` bash
# Install the Xaptum API repo GPG signing key.
apt-key adv --keyserver keyserver.ubuntu.com --recv-keys c615bfaa7fe1b4ca

# Add the repository to your APT sources.
echo "deb http://dl.bintray.com/xaptum/deb stretch main" > /etc/apt/sources.list.d/xaptum.list

# Install the library
sudo apt-get install tpmproxy-dkms
```

### From Source

#### Build Dependencies

* `make`
* `gcc`
* Linux kernel headers for your distribution

#### tpmproxy

```bash
make
sudo make modules_install
```

## License
Copyright (c) 2018 Xaptum, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
