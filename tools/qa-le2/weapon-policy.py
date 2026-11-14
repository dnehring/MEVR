# SPDX-License-Identifier: MIT
"""Source-audited base reserve getter chains; not runtime eligibility or save legality.

See research/le2-weapon-observation.md. Keep observer metadata validation separate.
"""

RESERVE_CLASSES = {
    'SFXGameContent_Inventory.SFXWeapon_HeavyPistol',
    'SFXGameContent_Inventory.SFXWeapon_AssaultRifle',
    'SFXGameContentDLC_PRE_Collectors.SFXWeapon_CollectorAssaultRifle_Player',
    'SFXGameContentDLC_Pistol.SFXWeapon_LaserPistol',
    'SFXGameContent_Inventory.SFXWeapon_MassCannon',
}
