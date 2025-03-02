The default mapping of the virtual modifier `Hyper` to `Mod3` is now deactivated
by default, because it conflicts with `LevelFive`, which is used in various layouts
with 5+ levels.

It is now enabled only when using any option binding `Hyper` keysyms or when
using the new option `hyper:mod3`. However, if one one want to use `LevelFive`
and `Hyper` simultaneously (e.g. for layouts with 5+ levels), then the new
alternative option `hyper:mod4` should be used instead.
