# Empros Quake II

Empros Quake II is an experimental enhanced client for id Software's Quake
II with focus on offline and coop gameplay. It's also a playground for me to
experiment with the implementation of some rendering effects and techniques.
These are subtle effects which do not aim to make Quake II look
like a modern AAA game (e.g. no over the top bump-mapping),
but instead try to preserve and enhance the original feel of the game.

Current features include:

- Render scaling for retro look without changing the window resolution
- HDR rendering
- Post-processing effects: bloom, ambient occlusion, motion blur
- Support for lightmapped water surfaces
- Realtime shadow mapping for dlights (experimental)
- Qbism's QBSP map format support for extended limits

Every feature is optional and the renderer was optimized so if you want a vanilla experience
you can still have it with pretty good performance for big maps.

This code is built upon Yamagi Quake II, which itself is based
on Icculus Quake II, which itself is based on Quake II 3.21.
Empros Quake II is released under the terms of the GPL version 2. See the
LICENSE file for further information.

## Documentation

Before asking any question, read through the documentation! The current
version can be found here: [doc/010_index.md](doc/010_index.md)

## Releases

Releases will be listed in this repository's [Releases](https://github.com/glhrmfrts/empros-quake2/releases) section.
