# Preserve conditional image results

The former image-bounds rewrite has been removed, including its environment
switch. An image operation followed by `OpSelect(..., value, zero)` does not
establish that the select is a bounds check. Replacing both alternatives with
`value` changes arbitrary shader control flow.

On x300/Mali-G1-Ultra, Hell Clock frame 10271 reproduced black characters and
foliage. GFXReconstruct capture SHA-256:
`405fa84db7f1f2be9ba08974384604eaeea8d7590017e2eb52aec51dd23759ed`.
Pipeline 613010's fragment shader selects 1 or 0 after a material comparison.
The rewrite forced 1, causing a subsequent material factor to become zero.
Replaying the unchanged game shaders with only this rewrite disabled restored
character and foliage colors. Replays with `-m none` and `-m rebind` produced
identical RGB output before this change.

After removing both call sites and rebuilding the isolated diagnostic layer,
replay without the disable variable or shader replacements produced identical
RGB pixels to the disabled-pass reference. Tested layer SHA-256:
`ae1c246a801270f59e175e1ff2729d50e230787666fa3b66eeff8699da7ceae4`.
This layer still contains separate diagnostic feature-advertisement changes;
it is not a production package or proof of complete BC support.

The old host test only checked that the select was changed and the resulting
SPIR-V remained structurally valid. It did not check semantic preservation and
was removed. This replay is evidence for the regression, not full game or driver
acceptance. Actual gameplay and other applications still require regression.

On-device check (2026-10-11): launched Hell Clock from Steam on x300 into the
separate Save II test slot with the rebuilt diagnostic layer above. The player
character's clothing and skin were colored; clicking the ground moved the
character and dismissed the movement tutorial. No shader replacement or
image-bounds environment override was used. This verifies live rendering and
basic movement, not combat, full BC conformance, or the production APK. The
original Save I was not selected.

The same session subsequently reached the attack tutorial. Shift + left-click
stationary shooting cleared the enemies and advanced to the interaction
tutorial. Player and enemy colors remained visible. This adds basic combat
evidence for the isolated layer, not production-driver acceptance or a full run.
