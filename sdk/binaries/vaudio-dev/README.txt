Vercidium Audio 1.10.1 dev build, for debugging the raytraced sound.

To see the rays in game, keep the production DLL and type snd_raytrace_draw 1 in the console with snd_raytrace on.

The dev build always opens its own debug window and sends it the whole level when the level loads.
On a level the size of Escape that load has stalled for 4 to 6 minutes, so it is not usable in this build yet.
To try it anyway, copy vaudionative.dll and glfw3.dll from this folder into bin, over the production vaudionative.dll there.
Copy the production vaudionative.dll back for normal play, since the dev build is slower.
