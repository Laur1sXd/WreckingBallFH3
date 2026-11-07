WreckingBall 1.0.0 for Forza Horizon 3
====================================

Requires Forza Horizon 3 Xbox One 1.0.125.0 on Azimuth and FH3 Mod
Platform. The settings preview supports platform API 14, 15 and 16.

INSTALLATION

1. Close Forza Horizon 3 and Azimuth.
2. Open the folder that contains Azimuth's executable.
3. Copy the archive's fh3-mods folder into that folder. Replace
   fh3-mods\WreckingBall.dll if upgrading.
4. Start FH3. Open Pause > MODS > Installed mods > WreckingBall.

If migrating from the ExtraOptions edition, remove its old DLL from
mods\WreckingBall\plugins before starting FH3.

SETTINGS

The DLL creates fh3-mods\WreckingBall.ini on first launch. Upgrades keep
the existing file. To get a new copy of the default settings and comments,
rename the INI while Azimuth is closed, then start the game again.

In MODS, use D-pad or left-stick Left/Right to preview values. Move between
rows to stage several changes, then press A to apply all of them or Back
to discard them. Confirmed settings are saved in the INI.
Scenery switches take effect during the session. Areas that loaded while a
conversion was off gain it when they stream in again. Restart Azimuth after
editing the INI by hand.

COLLISION AND DEBRIS

WreckingBall adds breakable physics to selected scenery instances with no
stock breakable group. Converted trees use FH3's separate trunk and branch
pieces with their matching ground collision. The mod prefers authored pieces
from the same tree family. Some trees borrow loaded wood fragments, and a
tree can break without debris when the required meshes are absent. Converted
barriers without authored pieces use their original mesh for loose debris.
FH3 handles its own bushes, crops and breakable utility poles. Older bush
settings in an existing INI have no effect.

Breakable benches covers the solid wood-topped benches in mainland
Australia, including the route variants. A converted bench leaves its
original model as one loose piece with ground collision and the normal
debris clean-up settings. If all three detail levels are unavailable,
the bench clears without spawning debris. FH3 handles the other six
bench families with its own break physics.

WreckingBall clears matched vertical faces from recognized procedural
collision meshes. Large palms across mainland Australia get a standing
collider sized to the trunk. Their matched stock trunk faces, including
the caps, clear when the native break runs. Buildings, terrain, roads,
floors and collision with ambiguous ownership keep their stock collision.

Railings also covers the audited white panels and posts over shallow
water at the two Surfers Paradise crossings. The Guardrails switch
covers the audited Armco sections at a third mainland bridge. The
bridge decks keep their stock collision.

F8 logs nearby scenery to fh3-mods\WreckingBall.log. Set Debug=1 for a
more detailed log.

Based on Trinculo54's WreckingBall for FH2, GPL-3.0-or-later.
Original project and license at https://github.com/Trinculo54/WreckingBall
Source for this FH3 port at https://github.com/Laur1sXd/WreckingBallFH3
