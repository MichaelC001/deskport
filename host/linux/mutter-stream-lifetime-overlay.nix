# Opt-in compositor fix. Adding DeskPort alone does not patch the running GNOME
# session. The system owner must select this overlay and activate it explicitly.
final: prev: {
  mutter = prev.mutter.overrideAttrs (old:
    assert prev.lib.assertMsg (builtins.elem old.version [ "51.0" "51.1" ])
      "DeskPort's Mutter stream-lifetime patch requires a reviewed Mutter 51.0 or 51.1 source";
    {
      patches = (old.patches or [ ]) ++ [ ./patches/mutter-51-stream-close-lifetime.patch ];
    });
}
