-- SPDX-License-Identifier: GPL-2.0-or-later

with HW.GFX.GMA;
with HW.GFX.GMA.Display_Probing;

use HW.GFX.GMA;
use HW.GFX.GMA.Display_Probing;

private package GMA.Mainboard is

   -- NUC5i5RYB external outputs: HDMI + mini-DisplayPort, no internal panel.
   ports : constant Port_List :=
     (HDMI1,
      HDMI2,
      HDMI3,
      DP1,
      DP2,
      DP3,
      others => Disabled);

end GMA.Mainboard;
