#!/bin/sh

apt-get autoremove -y --purge

# Clean up any build cache for rust.
rm -rf /.cargo

ccache --show-stats
