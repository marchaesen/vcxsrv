Rust
====

Rust Update Policy
------------------

Given that for some distributions it's not feasible to keep up with the
pace of Rust, we promise to only bump the minimum required Rust version
following those rules:

-  Only up to the Rust requirement of other major Linux desktop
   components, e.g.:

   -  `Firefox ESR <https://whattrainisitnow.com/release/?version=esr>`__:
      `Minimum Supported Rust Version:
      <https://firefox-source-docs.mozilla.org/writing-rust-code/update-policy.html#schedule>`__

   -  latest `Linux Kernel Rust requirement
      <https://docs.kernel.org/process/changes.html#current-minimal-requirements>`__

-  Only require a newer Rust version than stated by other rules if and only
   if it's required to get around a bug inside rustc.

As bug fixes might run into rustc compiler bugs, a rust version bump _can_
happen on a stable branch as well.
