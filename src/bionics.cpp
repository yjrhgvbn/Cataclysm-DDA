#include "bionics.h"
#include "player.h"

#include "action.h"
#include "ballistics.h"
#include "dispersion.h"
#include "game.h"
#include "map.h"
#include "map_iterator.h"
#include "debug.h"
#include "rng.h"
#include "input.h"
#include "item.h"
#include "bodypart.h"
#include "messages.h"
#include "overmapbuffer.h"
#include "projectile.h"
#include "string_formatter.h"
#include "sounds.h"
#include "translations.h"
#include "monster.h"
#include "overmap.h"
#include "itype.h"
#include "vehicle.h"
#include "field.h"
#include "weather.h"
#include "cata_utility.h"
#include "output.h"
#include "mutation.h"
#include "requirements.h"

#include <algorithm> //std::min
#include <sstream>

const skill_id skilll_electronics( "electronics" );
const skill_id skilll_firstaid( "firstaid" );
const skill_id skilll_mechanics( "mechanics" );

const efftype_id effect_adrenaline( "adrenaline" );
const efftype_id effect_adrenaline_mycus( "adrenaline_mycus" );
const efftype_id effect_bleed( "bleed" );
const efftype_id effect_bloodworms( "bloodworms" );
const efftype_id effect_brainworms( "brainworms" );
const efftype_id effect_cig( "cig" );
const efftype_id effect_datura( "datura" );
const efftype_id effect_dermatik( "dermatik" );
const efftype_id effect_drunk( "drunk" );
const efftype_id effect_fungus( "fungus" );
const efftype_id effect_hallu( "hallu" );
const efftype_id effect_high( "high" );
const efftype_id effect_iodine( "iodine" );
const efftype_id effect_meth( "meth" );
const efftype_id effect_paincysts( "paincysts" );
const efftype_id effect_pblue( "pblue" );
const efftype_id effect_pkill1( "pkill1" );
const efftype_id effect_pkill2( "pkill2" );
const efftype_id effect_pkill3( "pkill3" );
const efftype_id effect_pkill_l( "pkill_l" );
const efftype_id effect_poison( "poison" );
const efftype_id effect_stung( "stung" );
const efftype_id effect_tapeworm( "tapeworm" );
const efftype_id effect_teleglow( "teleglow" );
const efftype_id effect_tetanus( "tetanus" );
const efftype_id effect_took_flumed( "took_flumed" );
const efftype_id effect_took_prozac( "took_prozac" );
const efftype_id effect_took_xanax( "took_xanax" );
const efftype_id effect_visuals( "visuals" );
const efftype_id effect_weed_high( "weed_high" );

static const trait_id trait_PROF_MED( "PROF_MED" );
static const trait_id trait_NOPAIN( "NOPAIN" );
static const trait_id trait_PAINRESIST_TROGLO( "PAINRESIST_TROGLO" );
static const trait_id trait_PAINRESIST( "PAINRESIST" );
static const trait_id trait_CENOBITE( "CENOBITE" );
static const trait_id trait_MASOCHIST( "MASOCHIST" );
static const trait_id trait_MASOCHIST_MED( "MASOCHIST_MED" );

namespace
{
std::map<bionic_id, bionic_data> bionics;
std::vector<bionic_id> faulty_bionics;
} //namespace

/** @relates string_id */
template<>
bool string_id<bionic_data>::is_valid() const
{
    return bionics.count( *this ) > 0;
}

/** @relates string_id */
template<>
const bionic_data &string_id<bionic_data>::obj() const
{
    auto const it = bionics.find( *this );
    if( it != bionics.end() ) {
        return it->second;
    }

    debugmsg( "bad bionic id %s", c_str() );

    static bionic_data const null_value;
    return null_value;
}

bool bionic_data::is_included( const bionic_id &id ) const
{
    return std::find( included_bionics.begin(), included_bionics.end(), id ) != included_bionics.end();
}

void bionics_install_failure( player *u, int difficulty, int success );

bionic_data::bionic_data()
{
    name = "bad bionic";
    description = "This bionic was not set up correctly, this is a bug";
}

void force_comedown( effect &eff )
{
    if( eff.is_null() || eff.get_effect_type() == nullptr || eff.get_duration() <= 1 ) {
        return;
    }

    eff.set_duration( std::min( eff.get_duration(), eff.get_int_dur_factor() ) );
}

// Why put this in a Big Switch?  Why not let bionics have pointers to
// functions, much like monsters and items?
//
// Well, because like diseases, which are also in a Big Switch, bionics don't
// share functions....
bool player::activate_bionic( int b, bool eff_only )
{
    bionic &bio = ( *my_bionics )[b];

    // Preserve the fake weapon used to initiate bionic gun firing
    static item bio_gun( weapon );

    // Special compatibility code for people who updated saves with their claws out
    if( ( weapon.typeId() == "bio_claws_weapon" && bio.id == "bio_claws_weapon" ) ||
        ( weapon.typeId() == "bio_blade_weapon" && bio.id == "bio_blade_weapon" ) ) {
        return deactivate_bionic( b );
    }

    // eff_only means only do the effect without messing with stats or displaying messages
    if( !eff_only ) {
        if( bio.powered ) {
            // It's already on!
            return false;
        }
        if( power_level < bionics[bio.id].power_activate ) {
            add_msg( m_info, _( "You don't have the power to activate your %s." ),
                     bionics[bio.id].name.c_str() );
            return false;
        }

        //We can actually activate now, do activation-y things
        charge_power( -bionics[bio.id].power_activate );
        if( bionics[bio.id].toggled || bionics[bio.id].charge_time > 0 ) {
            bio.powered = true;
        }
        if( bionics[bio.id].charge_time > 0 ) {
            bio.charge = bionics[bio.id].charge_time;
        }
        add_msg( m_info, _( "You activate your %s." ), bionics[bio.id].name.c_str() );
    }

    item tmp_item;
    w_point const weatherPoint = *g->weather_precise;

    // On activation effects go here
    if( bionics[bio.id].gun_bionic ) {
        charge_power( bionics[bio.id].power_activate );
        bio_gun = item( bionics[bio.id].fake_item );
        g->refresh_all();
        g->plfire( bio_gun, bionics[bio.id].power_activate );
    } else if( bionics[ bio.id ].weapon_bionic ) {
        if( weapon.has_flag( "NO_UNWIELD" ) ) {
            add_msg( m_info, _( "Deactivate your %s first!" ), weapon.tname().c_str() );
            charge_power( bionics[bio.id].power_activate );
            bio.powered = false;
            return false;
        }

        if( !weapon.is_null() ) {
            add_msg( m_warning, _( "You're forced to drop your %s." ), weapon.tname().c_str() );
            g->m.add_item_or_charges( pos(), weapon );
        }

        weapon = item( bionics[bio.id].fake_item );
        weapon.invlet = '#';
    } else if( bio.id == "bio_ears" && has_active_bionic( bionic_id( "bio_earplugs" ) ) ) {
        for( auto &i : *my_bionics ) {
            if( i.id == "bio_earplugs" ) {
                i.powered = false;
                add_msg( m_info, _( "Your %s automatically turn off." ), bionics[i.id].name.c_str() );
            }
        }
    } else if( bio.id == "bio_earplugs" && has_active_bionic( bionic_id( "bio_ears" ) ) ) {
        for( auto &i : *my_bionics ) {
            if( i.id == "bio_ears" ) {
                i.powered = false;
                add_msg( m_info, _( "Your %s automatically turns off." ), bionics[i.id].name.c_str() );
            }
        }
    } else if( bio.id == "bio_tools" ) {
        invalidate_crafting_inventory();
    } else if( bio.id == "bio_cqb" ) {
        if( !pick_style() ) {
            bio.powered = false;
            add_msg( m_info, _( "You change your mind and turn it off." ) );
            return false;
        }
    } else if( bio.id == "bio_resonator" ) {
        //~Sound of a bionic sonic-resonator shaking the area
        sounds::sound( pos(), 30, _( "VRRRRMP!" ) );
        for( int i = posx() - 1; i <= posx() + 1; i++ ) {
            for( int j = posy() - 1; j <= posy() + 1; j++ ) {
                tripoint bashpoint( i, j, posz() );
                g->m.bash( bashpoint, 110 );
                g->m.bash( bashpoint, 110 ); // Multibash effect, so that doors &c will fall
                g->m.bash( bashpoint, 110 );
            }
        }

        mod_moves( -100 );
    } else if( bio.id == "bio_time_freeze" ) {
        mod_moves( power_level );
        power_level = 0;
        add_msg( m_good, _( "Your speed suddenly increases!" ) );
        if( one_in( 3 ) ) {
            add_msg( m_bad, _( "Your muscles tear with the strain." ) );
            apply_damage( nullptr, bp_arm_l, rng( 5, 10 ) );
            apply_damage( nullptr, bp_arm_r, rng( 5, 10 ) );
            apply_damage( nullptr, bp_leg_l, rng( 7, 12 ) );
            apply_damage( nullptr, bp_leg_r, rng( 7, 12 ) );
            apply_damage( nullptr, bp_torso, rng( 5, 15 ) );
        }
        if( one_in( 5 ) ) {
            add_effect( effect_teleglow, rng( 50, 400 ) );
        }
    } else if( bio.id == "bio_teleport" ) {
        g->teleport();
        add_effect( effect_teleglow, 300 );
        mod_moves( -100 );
    } else if( bio.id == "bio_blood_anal" ) {
        static const std::map<efftype_id, std::string> bad_effects = {{
                { effect_fungus, _( "Fungal Infection" ) },
                { effect_dermatik, _( "Insect Parasite" ) },
                { effect_stung, _( "Stung" ) },
                { effect_poison, _( "Poison" ) },
                // Those may be good for the player, but the scanner doesn't like them
                { effect_drunk, _( "Alcohol" ) },
                { effect_cig, _( "Nicotine" ) },
                { effect_meth, _( "Methamphetamines" ) },
                { effect_high, _( "Intoxicant: Other" ) },
                { effect_weed_high, _( "THC Intoxication" ) },
                // This little guy is immune to the blood filter though, as he lives in your bowels.
                { effect_tapeworm, _( "Intestinal Parasite" ) },
                { effect_bloodworms, _( "Hemolytic Parasites" ) },
                // These little guys are immune to the blood filter too, as they live in your brain.
                { effect_brainworms, _( "Intracranial Parasites" ) },
                // These little guys are immune to the blood filter too, as they live in your muscles.
                { effect_paincysts, _( "Intramuscular Parasites" ) },
                // Tetanus infection.
                { effect_tetanus, _( "Clostridium Tetani Infection" ) },
                { effect_datura, _( "Anticholinergic Tropane Alkaloids" ) },
                // @todo Hallucinations not inducted by chemistry
                { effect_hallu, _( "Hallucinations" ) },
                { effect_visuals, _( "Hallucinations" ) },
            }
        };

        static const std::map<efftype_id, std::string> good_effects = {{
                { effect_pkill1, _( "Minor Painkiller" ) },
                { effect_pkill2, _( "Moderate Painkiller" ) },
                { effect_pkill3, _( "Heavy Painkiller" ) },
                { effect_pkill_l, _( "Slow-Release Painkiller" ) },

                { effect_pblue, _( "Prussian Blue" ) },
                { effect_iodine, _( "Potassium Iodide" ) },

                { effect_took_xanax, _( "Xanax" ) },
                { effect_took_prozac, _( "Prozac" ) },
                { effect_took_flumed, _( "Antihistamines" ) },
                { effect_adrenaline, _( "Adrenaline Spike" ) },
                // Should this be described like that? Does the bionic know what is this?
                { effect_adrenaline_mycus, _( "Mycal Spike" ) },
            }
        };

        std::vector<std::string> good;
        std::vector<std::string> bad;

        if( radiation > 0 ) {
            bad.push_back( _( "Irradiated" ) );
        }

        // @todo Expose the player's effects to check it in a cleaner way
        for( const auto &pr : bad_effects ) {
            if( has_effect( pr.first ) ) {
                bad.push_back( pr.second );
            }
        }

        for( const auto &pr : good_effects ) {
            if( has_effect( pr.first ) ) {
                good.push_back( pr.second );
            }
        }

        const size_t win_h = std::min( static_cast<size_t>( TERMY ), bad.size() + good.size() + 2 );
        const int win_w = 46;
        WINDOW *w = newwin( win_h, win_w, ( TERMY - win_h ) / 2, ( TERMX - win_w ) / 2 );
        draw_border( w, c_red, string_format( " %s ", _( "Blood Test Results" ) ) );
        if( good.empty() && bad.empty() ) {
            trim_and_print( w, 1, 2, win_w - 3, c_white, _( "No effects." ) );
        } else {
            for( size_t line = 1; line < ( win_h - 1 ) && line <= good.size() + bad.size(); ++line ) {
                if( line <= bad.size() ) {
                    trim_and_print( w, line, 2, win_w - 3, c_red, "%s", bad[line - 1].c_str() );
                } else {
                    trim_and_print( w, line, 2, win_w - 3, c_green, "%s",
                                    good[line - 1 - bad.size()].c_str() );
                }
            }
        }
        wrefresh( w );
        refresh();
        inp_mngr.wait_for_any_key();
        delwin( w );
    } else if( bio.id == "bio_blood_filter" ) {
        static const std::vector<efftype_id> removable = {{
                effect_fungus, effect_dermatik, effect_bloodworms,
                effect_tetanus, effect_poison, effect_stung,
                effect_pkill1, effect_pkill2, effect_pkill3, effect_pkill_l,
                effect_drunk, effect_cig, effect_high, effect_hallu, effect_visuals,
                effect_pblue, effect_iodine, effect_datura,
                effect_took_xanax, effect_took_prozac, effect_took_flumed,
            }
        };

        for( const auto &eff : removable ) {
            remove_effect( eff );
        }
        // Purging the substance won't remove the fatigue it caused
        force_comedown( get_effect( effect_adrenaline ) );
        force_comedown( get_effect( effect_meth ) );
        set_painkiller( 0 );
        stim = 0;
        mod_moves( -100 );
    } else if( bio.id == "bio_evap" ) {
        item water = item( "water_clean", 0 );
        int humidity = weatherPoint.humidity;
        int water_charges = ( humidity * 3.0 ) / 100.0 + 0.5;
        // At 50% relative humidity or more, the player will draw 2 units of water
        // At 16% relative humidity or less, the player will draw 0 units of water
        water.charges = water_charges;
        if( water_charges == 0 ) {
            add_msg_if_player( m_bad,
                               _( "There was not enough moisture in the air from which to draw water!" ) );
        } else if( !g->consume_liquid( water ) ) {
            charge_power( bionics[bionic_id( "bio_evap" )].power_activate );
        }
    } else if( bio.id == "bio_lighter" ) {
        g->refresh_all();
        tripoint dirp;
        if( choose_adjacent( _( "Start a fire where?" ), dirp ) &&
            g->m.add_field( dirp, fd_fire, 1, 0 ) ) {
            mod_moves( -100 );
        } else {
            add_msg_if_player( m_info, _( "You can't light a fire there." ) );
            charge_power( bionics[bionic_id( "bio_lighter" )].power_activate );
        }
    } else if( bio.id == "bio_geiger" ) {
        add_msg( m_info, _( "Your radiation level: %d" ), radiation );
    } else if( bio.id == "bio_radscrubber" ) {
        if( radiation > 4 ) {
            radiation -= 5;
        } else {
            radiation = 0;
        }
    } else if( bio.id == "bio_adrenaline" ) {
        if( has_effect( effect_adrenaline ) ) {
            // Safety
            add_msg_if_player( m_bad, _( "The bionic refuses to activate!" ) );
            charge_power( bionics[bio.id].power_activate );
        } else {
            add_effect( effect_adrenaline, 200 );
        }

    } else if( bio.id == "bio_emp" ) {
        g->refresh_all();
        tripoint dirp;
        if( choose_adjacent( _( "Create an EMP where?" ), dirp ) ) {
            g->emp_blast( dirp );
            mod_moves( -100 );
        } else {
            charge_power( bionics[bionic_id( "bio_emp" )].power_activate );
        }
    } else if( bio.id == "bio_hydraulics" ) {
        add_msg( m_good, _( "Your muscles hiss as hydraulic strength fills them!" ) );
        //~ Sound of hissing hydraulic muscle! (not quite as loud as a car horn)
        sounds::sound( pos(), 19, _( "HISISSS!" ) );
    } else if( bio.id == "bio_water_extractor" ) {
        bool extracted = false;
        for( auto it = g->m.i_at( pos() ).begin();
             it != g->m.i_at( pos() ).end(); ++it ) {
            static const auto volume_per_water_charge = units::from_milliliter( 500 );
            if( it->is_corpse() ) {
                const int avail = it->get_var( "remaining_water", it->volume() / volume_per_water_charge );
                if( avail > 0 && query_yn( _( "Extract water from the %s" ), it->tname().c_str() ) ) {
                    item water( "water_clean", calendar::turn, avail );
                    if( g->consume_liquid( water ) ) {
                        extracted = true;
                        it->set_var( "remaining_water", static_cast<int>( water.charges ) );
                    }
                    break;
                }
            }
        }
        if( !extracted ) {
            charge_power( bionics[bionic_id( "bio_water_extractor" )].power_activate );
        }
    } else if( bio.id == "bio_magnet" ) {
        static const std::set<material_id> affected_materials =
        { material_id( "iron" ), material_id( "steel" ) };
        // Remember all items that will be affected, then affect them
        // Don't "snowball" by affecting some items multiple times
        std::vector<std::pair<item, tripoint>> affected;
        const auto weight_cap = weight_capacity();
        for( const tripoint &p : g->m.points_in_radius( pos(), 10 ) ) {
            if( p == pos() || !g->m.has_items( p ) || g->m.has_flag( "SEALED", p ) ) {
                continue;
            }

            auto stack = g->m.i_at( p );
            for( auto it = stack.begin(); it != stack.end(); it++ ) {
                if( it->weight() < weight_cap &&
                    it->made_of_any( affected_materials ) ) {
                    affected.emplace_back( std::make_pair( *it, p ) );
                    stack.erase( it );
                    break;
                }
            }
        }

        g->refresh_all();
        for( const auto &pr : affected ) {
            projectile proj;
            proj.speed  = 50;
            proj.impact = damage_instance::physical( pr.first.weight() / 250_gram, 0, 0, 0 );
            // make the projectile stop one tile short to prevent hitting the player
            proj.range = rl_dist( pr.second, pos() ) - 1;
            proj.proj_effects = {{ "NO_ITEM_DAMAGE", "DRAW_AS_LINE", "NO_DAMAGE_SCALING", "JET" }};

            auto dealt = projectile_attack( proj, pr.second, pos(), 0 );
            g->m.add_item_or_charges( dealt.end_point, pr.first );
        }

        mod_moves( -100 );
    } else if( bio.id == "bio_lockpick" ) {
        tmp_item = item( "pseuso_bio_picklock", 0 );
        g->refresh_all();
        if( invoke_item( &tmp_item ) == 0 ) {
            if( tmp_item.charges > 0 ) {
                // restore the energy since CBM wasn't used
                charge_power( bionics[bio.id].power_activate );
            }
            return true;
        }

        mod_moves( -100 );
    } else if( bio.id == "bio_flashbang" ) {
        g->flashbang( pos(), true );
        mod_moves( -100 );
    } else if( bio.id == "bio_shockwave" ) {
        g->shockwave( pos(), 3, 4, 2, 8, true );
        add_msg_if_player( m_neutral, _( "You unleash a powerful shockwave!" ) );
        mod_moves( -100 );
    } else if( bio.id == "bio_meteorologist" ) {
        // Calculate local wind power
        int vpart = -1;
        vehicle *veh = g->m.veh_at( pos(), vpart );
        int vehwindspeed = 0;
        if( veh != nullptr ) {
            vehwindspeed = abs( veh->velocity / 100 ); // vehicle velocity in mph
        }
        const oter_id &cur_om_ter = overmap_buffer.ter( global_omt_location() );
        /* windpower defined in internal velocity units (=.01 mph) */
        double windpower = 100.0f * get_local_windpower( weatherPoint.windpower + vehwindspeed,
                           cur_om_ter, g->is_sheltered( g->u.pos() ) );
        add_msg_if_player( m_info, _( "Temperature: %s." ),
                           print_temperature( g->get_temperature() ).c_str() );
        add_msg_if_player( m_info, _( "Relative Humidity: %s." ),
                           print_humidity(
                               get_local_humidity( weatherPoint.humidity, g->weather,
                                       g->is_sheltered( g->u.pos() ) ) ).c_str() );
        add_msg_if_player( m_info, _( "Pressure: %s." ),
                           print_pressure( ( int )weatherPoint.pressure ).c_str() );
        add_msg_if_player( m_info, _( "Wind Speed: %.1f %s." ),
                           convert_velocity( int( windpower ), VU_WIND ),
                           velocity_units( VU_WIND ) );
        add_msg_if_player( m_info, _( "Feels Like: %s." ),
                           print_temperature(
                               get_local_windchill( weatherPoint.temperature, weatherPoint.humidity,
                                       windpower ) + g->get_temperature() ).c_str() );
    } else if( bio.id == "bio_remote" ) {
        int choice = menu( true, _( "Perform which function:" ), _( "Nothing" ),
                           _( "Control vehicle" ), _( "RC radio" ), NULL );
        if( choice >= 2 && choice <= 3 ) {
            item ctr;
            if( choice == 2 ) {
                ctr = item( "remotevehcontrol", 0 );
            } else {
                ctr = item( "radiocontrol", 0 );
            }
            ctr.charges = power_level;
            int power_use = invoke_item( &ctr );
            charge_power( -power_use );
            bio.powered = ctr.active;
        } else {
            bio.powered = g->remoteveh() != nullptr || get_value( "remote_controlling" ) != "";
        }
    } else if( bio.id == "bio_plutdump" ) {
        if( query_yn(
                _( "WARNING: Purging all fuel is likely to result in radiation!  Purge anyway?" ) ) ) {
            slow_rad += ( tank_plut + reactor_plut );
            tank_plut = 0;
            reactor_plut = 0;
        }
    } else if( bio.id == "bio_cable" ) {
        bool has_cable = has_item_with( []( const item & it ) {
            return it.active && it.has_flag( "CABLE_SPOOL" );
        } );

        if( !has_cable ) {
            add_msg_if_player( m_info,
                               _( "You need a jumper cable connected to a vehicle to drain power from it." ) );
        }
    }

    // Recalculate stats (strength, mods from pain etc.) that could have been affected
    reset();

    // Also reset crafting inventory cache if this bionic spawned a fake item
    if( !bionics[ bio.id ].fake_item.empty() ) {
        invalidate_crafting_inventory();
    }

    return true;
}

bool player::deactivate_bionic( int b, bool eff_only )
{
    bionic &bio = ( *my_bionics )[b];

    // Just do the effect, no stat changing or messages
    if( !eff_only ) {
        if( !bio.powered ) {
            // It's already off!
            return false;
        }
        if( !bionics[bio.id].toggled ) {
            // It's a fire-and-forget bionic, we can't turn it off but have to wait for it to run out of charge
            add_msg( m_info, _( "You can't deactivate your %s manually!" ), bionics[bio.id].name.c_str() );
            return false;
        }
        if( power_level < bionics[bio.id].power_deactivate ) {
            add_msg( m_info, _( "You don't have the power to deactivate your %s." ),
                     bionics[bio.id].name.c_str() );
            return false;
        }

        //We can actually deactivate now, do deactivation-y things
        charge_power( -bionics[bio.id].power_deactivate );
        bio.powered = false;
        add_msg( m_neutral, _( "You deactivate your %s." ), bionics[bio.id].name.c_str() );
    }

    // Deactivation effects go here
    if( bionics[ bio.id ].weapon_bionic ) {
        if( weapon.typeId() == bionics[ bio.id ].fake_item ) {
            add_msg( _( "You withdraw your %s." ), weapon.tname().c_str() );
            weapon = ret_null;
        }
    } else if( bio.id == "bio_cqb" ) {
        // check if player knows current style naturally, otherwise drop them back to style_none
        if( style_selected != matype_id( "style_none" ) && style_selected != matype_id( "style_kicks" ) ) {
            bool has_style = false;
            for( auto &elem : ma_styles ) {
                if( elem == style_selected ) {
                    has_style = true;
                }
            }
            if( !has_style ) {
                style_selected = matype_id( "style_none" );
            }
        }
    } else if( bio.id == "bio_remote" ) {
        if( g->remoteveh() != nullptr && !has_active_item( "remotevehcontrol" ) ) {
            g->setremoteveh( nullptr );
        } else if( get_value( "remote_controlling" ) != "" && !has_active_item( "radiocontrol" ) ) {
            set_value( "remote_controlling", "" );
        }
    } else if( bio.id == "bio_tools" ) {
        invalidate_crafting_inventory();
    }

    // Recalculate stats (strength, mods from pain etc.) that could have been affected
    reset();

    // Also reset crafting inventory cache if this bionic spawned a fake item
    if( !bionics[ bio.id ].fake_item.empty() ) {
        invalidate_crafting_inventory();
    }

    return true;
}

/**
 * @param p the player
 * @param bio the bionic that is meant to be recharged.
 * @param amount the amount of power that is to be spent recharging the bionic.
 * @param factor multiplies the power cost per turn.
 * @param rate divides the number of turns we may charge (rate of 2 discharges in half the time).
 * @return indicates whether we successfully charged the bionic.
 */
bool attempt_recharge( player &p, bionic &bio, int &amount, int factor = 1, int rate = 1 )
{
    bionic_data const &info = bio.info();
    const int armor_power_cost = 1;
    int power_cost = info.power_over_time * factor;
    bool recharged = false;

    if( power_cost > 0 ) {
        if( info.armor_interface ) {
            // Don't spend any power on armor interfacing unless we're wearing active powered armor.
            bool powered_armor = std::any_of( p.worn.begin(), p.worn.end(),
            []( const item & w ) {
                return w.active && w.is_power_armor();
            } );
            if( !powered_armor ) {
                power_cost -= armor_power_cost * factor;
            }
        }
        if( p.power_level >= power_cost ) {
            // Set the recharging cost and charge the bionic.
            amount = power_cost;
            // This is our first turn of charging, so subtract a turn from the recharge delay.
            bio.charge = info.charge_time - rate;
            recharged = true;
        }
    }

    return recharged;
}

void player::process_bionic( int b )
{
    bionic &bio = ( *my_bionics )[b];
    // Only powered bionics should be processed
    if( !bio.powered ) {
        return;
    }

    // These might be affected by environmental conditions, status effects, faulty bionics, etc.
    int discharge_factor = 1;
    int discharge_rate = 1;

    if( bio.charge > 0 ) {
        bio.charge -= discharge_rate;
    } else {
        if( bio.info().charge_time > 0 ) {
            // Try to recharge our bionic if it is made for it
            int cost = 0;
            bool recharged = attempt_recharge( *this, bio, cost, discharge_factor, discharge_rate );
            if( !recharged ) {
                // No power to recharge, so deactivate
                bio.powered = false;
                add_msg( m_neutral, _( "Your %s powers down." ), bio.info().name.c_str() );
                // This purposely bypasses the deactivation cost
                deactivate_bionic( b, true );
                return;
            }
            if( cost ) {
                charge_power( -cost );
            }
        }
    }

    // Bionic effects on every turn they are active go here.
    if( bio.id == "bio_night" ) {
        if( calendar::once_every( 5 ) ) {
            add_msg( m_neutral, _( "Artificial night generator active!" ) );
        }
    } else if( bio.id == "bio_remote" ) {
        if( g->remoteveh() == nullptr && get_value( "remote_controlling" ) == "" ) {
            bio.powered = false;
            add_msg( m_warning, _( "Your %s has lost connection and is turning off." ),
                     bionics[bio.id].name.c_str() );
        }
    } else if( bio.id == "bio_hydraulics" ) {
        // Sound of hissing hydraulic muscle! (not quite as loud as a car horn)
        sounds::sound( pos(), 19, _( "HISISSS!" ) );
    } else if( bio.id == "bio_nanobots" ) {
        for( int i = 0; i < num_hp_parts; i++ ) {
            if( power_level >= 5 && hp_cur[i] > 0 && hp_cur[i] < hp_max[i] ) {
                heal( ( hp_part )i, 1 );
                charge_power( -5 );
            }
        }
        for( int i = 0; i < num_bp; i++ ) {
            if( power_level >= 2 && remove_effect( effect_bleed, ( body_part )i ) ) {
                charge_power( -2 );
            }
        }
    } else if( bio.id == "bio_painkiller" ) {
        const int pkill = get_painkiller();
        const int pain = get_pain();
        int max_pkill = std::min( 150, pain );
        if( pkill < max_pkill ) {
            mod_painkiller( 1 );
            charge_power( -2 );
        }

        // Only dull pain so extreme that we can't pkill it safely
        if( pkill >= 150 && pain > pkill && stim > -150 ) {
            mod_pain( -1 );
            // Negative side effect: negative stim
            stim--;
            charge_power( -2 );
        }
    } else if( bio.id == "bio_cable" ) {
        if( power_level >= max_power_level ) {
            return;
        }

        const std::vector<item *> cables = items_with( []( const item & it ) {
            return it.active && it.has_flag( "CABLE_SPOOL" );
        } );

        constexpr int battery_per_power = 10;
        int wants_power_amt = battery_per_power;
        for( const item *cable : cables ) {
            const auto &target = cable->get_cable_target();
            vehicle *veh = g->m.veh_at( target );
            if( veh == nullptr ) {
                continue;
            }

            wants_power_amt = veh->discharge_battery( wants_power_amt );
            if( wants_power_amt == 0 ) {
                charge_power( 1 );
                break;
            }
        }

        if( wants_power_amt < battery_per_power &&
            wants_power_amt > 0 &&
            x_in_y( battery_per_power - wants_power_amt, battery_per_power ) ) {
            charge_power( 1 );
        }
    }
}

void bionics_uninstall_failure( player *u )
{
    switch( rng( 1, 5 ) ) {
        case 1:
            add_msg( m_neutral, _( "You flub the removal." ) );
            break;
        case 2:
            add_msg( m_neutral, _( "You mess up the removal." ) );
            break;
        case 3:
            add_msg( m_neutral, _( "The removal fails." ) );
            break;
        case 4:
            add_msg( m_neutral, _( "The removal is a failure." ) );
            break;
        case 5:
            add_msg( m_neutral, _( "You screw up the removal." ) );
            break;
    }
    add_msg( m_bad, _( "Your body is severely damaged!" ) );
    u->hurtall( rng( 30, 80 ), u ); // stop hurting yourself!
}

// bionic manipulation chance of success
int bionic_manip_cos( int p_int, int s_electronics, int s_firstaid, int s_mechanics,
                      int bionic_difficulty )
{
    int pl_skill = p_int         * 4 +
                   s_electronics * 4 +
                   s_firstaid    * 3 +
                   s_mechanics   * 1;

    // Medical residents have some idea what they're doing
    if( g->u.has_trait( trait_PROF_MED ) ) {
        pl_skill += 3;
        add_msg( m_neutral, _( "You prep yourself to begin surgery." ) );
    }

    // for chance_of_success calculation, shift skill down to a float between ~0.4 - 30
    float adjusted_skill = float ( pl_skill ) - std::min( float ( 40 ),
                           float ( pl_skill ) - float ( pl_skill ) / float ( 10.0 ) );

    // we will base chance_of_success on a ratio of skill and difficulty
    // when skill=difficulty, this gives us 1.  skill < difficulty gives a fraction.
    float skill_difficulty_parameter = float( adjusted_skill / ( 4.0 * bionic_difficulty ) );

    // when skill == difficulty, chance_of_success is 50%. Chance of success drops quickly below that
    // to reserve bionics for characters with the appropriate skill.  For more difficult bionics, the
    // curve flattens out just above 80%
    int chance_of_success = int( ( 100 * skill_difficulty_parameter ) /
                                 ( skill_difficulty_parameter + sqrt( 1 / skill_difficulty_parameter ) ) );

    return chance_of_success;
}

bool player::uninstall_bionic( bionic_id const &b_id, int skill_level )
{
    // malfunctioning bionics don't have associated items and get a difficulty of 12
    int difficulty = 12;
    const inventory &crafting_inv = crafting_inventory();
    if( item::type_is_defined( b_id.c_str() ) ) {
        auto type = item::find_type( b_id.c_str() );
        if( type->bionic ) {
            difficulty = type->bionic->difficulty;
        }
    }

    if( !has_bionic( b_id ) ) {
        popup( _( "You don't have this bionic installed." ) );
        return false;
    }
    //If you are paying the doctor to do it, shouldn't use your supplies
    static const quality_id CUT_FINE( "CUT_FINE" );
    if( !( crafting_inv.has_quality( CUT_FINE ) && crafting_inv.has_amount( "1st_aid", 1 ) ) &&
        skill_level == -1 ) {
        popup( _( "Removing bionics requires a tool with %s quality, and a first aid kit." ),
               CUT_FINE.obj().name.c_str() );
        return false;
    }

    if( b_id == "bio_blaster" ) {
        popup( _( "Removing your Fusion Blaster Arm would leave you with a useless stump." ) );
        return false;
    }

    if( ( b_id == "bio_reactor" ) || ( b_id == "bio_advreactor" ) ) {
        if( !query_yn(
                _( "WARNING: Removing a reactor may leave radioactive material! Remove anyway?" ) ) ) {
            return false;
        }
    }

    for( const auto &e : bionics ) {
        if( e.second.is_included( b_id ) ) {
            popup( _( "You must remove the %s bionic to remove the %s." ), e.second.name.c_str(),
                   b_id->name.c_str() );
            return false;
        }
    }

    if( b_id == "bio_eye_optic" ) {
        popup( _( "The Telescopic Lenses are part of your eyes now.  Removing them would leave you blind." ) );
        return false;
    }

    // removal of bionics adds +2 difficulty over installation, high quality tool substracts its fine cutting quality amount
    int chance_of_success;
    if( skill_level != -1 ) {
        chance_of_success = bionic_manip_cos( skill_level,
                                              skill_level,
                                              skill_level,
                                              skill_level,
                                              difficulty + 2 - crafting_inv.max_quality( CUT_FINE ) );
    } else {
        ///\EFFECT_INT increases chance of success removing bionics with unspecified skill level
        chance_of_success = bionic_manip_cos( int_cur,
                                              get_skill_level( skilll_electronics ),
                                              get_skill_level( skilll_firstaid ),
                                              get_skill_level( skilll_mechanics ),
                                              difficulty + 2 - crafting_inv.max_quality( CUT_FINE ) );
    }

    if( !query_yn(
            _( "WARNING: %i percent chance of failure and SEVERE bodily damage! Remove anyway?" ),
            100 - chance_of_success ) ) {
        return false;
    }

    // Surgery is imminent, retract claws or blade if active
    if( skill_level == -1 ) {
        for( size_t i = 0; i < my_bionics->size(); i++ ) {
            const auto &bio = ( *my_bionics )[ i ];
            if( bio.powered && bio.info().weapon_bionic ) {
                deactivate_bionic( i );
            }
        }
    }

    //If you are paying the doctor to do it, shouldn't use your supplies
    if( skill_level == -1 ) {
        std::vector<item_comp> comps;
        comps.push_back( item_comp( "1st_aid", 1 ) );
        consume_items( comps );
        invalidate_crafting_inventory();
    }

    practice( skilll_electronics, int( ( 100 - chance_of_success ) * 1.5 ) );
    practice( skilll_firstaid, int( ( 100 - chance_of_success ) * 1.0 ) );
    practice( skilll_mechanics, int( ( 100 - chance_of_success ) * 0.5 ) );

    int success = chance_of_success - rng( 1, 100 );

    if( success > 0 ) {
        add_memorial_log( pgettext( "memorial_male", "Removed bionic: %s." ),
                          pgettext( "memorial_female", "Removed bionic: %s." ),
                          bionics[b_id].name.c_str() );
        // until bionics can be flagged as non-removable
        add_msg( m_neutral, _( "You jiggle your parts back into their familiar places." ) );
        add_msg( m_good, _( "Successfully removed %s." ), bionics[b_id].name.c_str() );
        // remove power bank provided by bionic
        max_power_level -= bionics[b_id].capacity;
        remove_bionic( b_id );
        g->m.spawn_item( pos(), "burnt_out_bionic", 1 );
    } else {
        add_memorial_log( pgettext( "memorial_male", "Removed bionic: %s." ),
                          pgettext( "memorial_female", "Removed bionic: %s." ),
                          bionics[b_id].name.c_str() );
        bionics_uninstall_failure( this );
    }
    g->refresh_all();
    return true;
}

bool player::install_bionics( const itype &type, int skill_level )
{
    if( type.bionic.get() == nullptr ) {
        debugmsg( "Tried to install NULL bionic" );
        return false;
    }
    const bionic_id &bioid = type.bionic->id;
    if( !bioid.is_valid() ) {
        popup( "invalid / unknown bionic id %s", bioid.c_str() );
        return false;
    }
    if( bioid == "bio_reactor_upgrade" ) {
        if( !has_bionic( bionic_id( "bio_reactor" ) ) ) {
            popup( _( "There is nothing to upgrade!" ) );
            return false;
        }
    }
    if( has_bionic( bioid ) ) {
        if( !( bioid == "bio_power_storage" || bioid == "bio_power_storage_mkII" ) ) {
            popup( _( "You have already installed this bionic." ) );
            return false;
        }
    }
    const int difficult = type.bionic->difficulty;
    int chance_of_success;
    if( skill_level != -1 ) {
        chance_of_success = bionic_manip_cos( skill_level,
                                              skill_level,
                                              skill_level,
                                              skill_level,
                                              difficult );
    } else {
        ///\EFFECT_INT increases chance of success installing bionics with unspecified skill level
        chance_of_success = bionic_manip_cos( int_cur,
                                              get_skill_level( skilll_electronics ),
                                              get_skill_level( skilll_firstaid ),
                                              get_skill_level( skilll_mechanics ),
                                              difficult );
    }

    const std::map<body_part, int> &issues = bionic_installation_issues( bioid );
    // show all requirements which are not satisfied
    if( !issues.empty() ) {
        std::string detailed_info;
        for( auto &elem : issues ) {
            //~ <Body part name>: <number of slots> more slot(s) needed.
            detailed_info += string_format( _( "\n%s: %i more slot(s) needed." ),
                                            body_part_name_as_heading( elem.first, 1 ).c_str(),
                                            elem.second );
        }
        popup( _( "Not enough space for bionic installation!%s" ), detailed_info.c_str() );
        return false;
    }

    const int pk = get_painkiller();
    const int overall_pk_dur = ( get_effect_dur( effect_pkill1 ) + get_effect_dur( effect_pkill2 ) +
                                 get_effect_dur( effect_pkill3 ) + get_effect_dur( effect_pkill_l ) ) / MINUTES( 1 );
    int pain_cap = 100;
    if( has_trait( trait_PAINRESIST_TROGLO ) ) {
        pain_cap = pain_cap / 2;
    } else if( has_trait( trait_PAINRESIST ) ) {
        pain_cap = pain_cap / 1.5;
    }

    int fa_level = get_skill_level( skilll_firstaid );

    if( has_trait( trait_PROF_MED ) ) {
        fa_level = 5;
    }

    if( !has_trait( trait_NOPAIN ) && !has_trait( trait_CENOBITE ) &&
        !has_trait( trait_MASOCHIST_MED ) && !has_bionic( bionic_id( "bio_painkiller" ) ) ) {
        if( pk == 0 ) {
            popup( _( "You need to take painkillers to make installing bionics tolerable." ) );
            return false;
        } else if( pk < pain_cap / 2 ) {
            if( fa_level < 2 ) {
                popup( _( "You need to be a lot more numb to tolerate installing bionics.  "
                          "Note that painkillers you've already taken could take up to an hour"
                          " to achieve full effect." ) );
            } else if( fa_level <= 4 ) {
                popup( _( "Intensity of painkillers you've already taken is less than half of "
                          "the threshold that will allow you to install bionics.  It will take %i "
                          "minutes for painkillers you've already taken to achieve maximum effect."
                        ),
                       overall_pk_dur );
            } else {
                popup( _( "Intensity of painkillers you've already taken is %i percent of the "
                          "threshold that will allow you to install bionics.  It will take %i "
                          "minutes for painkillers you've already taken to achieve maximum effect."
                        ),
                       100 * pk / pain_cap, overall_pk_dur );
            }
            return false;
        } else if( pk < pain_cap ) {
            if( fa_level < 2 ) {
                popup( _( "You aren't quite numb enough to tolerate installing bionics.  Note that"
                          " painkillers you've already taken could take up to an hour to achieve "
                          "full effect." ) );
            } else if( fa_level <= 4 ) {
                popup( _( "Intensity of painkillers you've already taken is more than half of the "
                          "threshold that will allow you to install bionics.  It will take %i "
                          "minutes for painkillers you've already taken to achieve maximum effect."
                        ),
                       overall_pk_dur );
            } else {
                popup( _( "Intensity of painkillers you've already taken is %i percent of the "
                          "threshold that will allow you to install bionics.  It will take %i "
                          "minutes for painkillers you've already taken to achieve maximum effect."
                        ),
                       100 * pk / pain_cap, overall_pk_dur );
            }
            return false;
        }
    }

    if( !query_yn(
            _( "WARNING: %i percent chance of genetic damage, blood loss, or damage to existing bionics! Continue anyway?" ),
            ( 100 - int( chance_of_success ) ) ) ) {
        return false;
    }

    practice( skilll_electronics, int( ( 100 - chance_of_success ) * 1.5 ) );
    practice( skilll_firstaid, int( ( 100 - chance_of_success ) * 1.0 ) );
    practice( skilll_mechanics, int( ( 100 - chance_of_success ) * 0.5 ) );
    int success = chance_of_success - rng( 0, 99 );
    if( success > 0 ) {
        add_memorial_log( pgettext( "memorial_male", "Installed bionic: %s." ),
                          pgettext( "memorial_female", "Installed bionic: %s." ),
                          bioid->name.c_str() );

        add_msg( m_good, _( "Successfully installed %s." ), bioid->name.c_str() );
        add_bionic( bioid );

        for( const auto &mid : bioid->canceled_mutations ) {
            if( has_trait( mid ) ) {
                remove_mutation( mid );
            }
        }

        if( bioid == "bio_reactor_upgrade" ) {
            remove_bionic( bionic_id( "bio_reactor" ) );
            remove_bionic( bionic_id( "bio_reactor_upgrade" ) );
            add_bionic( bionic_id( "bio_advreactor" ) );
        }
    } else {
        add_memorial_log( pgettext( "memorial_male", "Installed bionic: %s." ),
                          pgettext( "memorial_female", "Installed bionic: %s." ),
                          bioid->name.c_str() );
        bionics_install_failure( this, difficult, success );
    }
    g->refresh_all();
    return true;
}

void bionics_install_failure( player *u, int difficulty, int success )
{
    // "success" should be passed in as a negative integer representing how far off we
    // were for a successful install.  We use this to determine consequences for failing.
    success = abs( success );

    // it would be better for code reuse just to pass in skill as an argument from install_bionic
    // pl_skill should be calculated the same as in install_bionics
    ///\EFFECT_INT randomly decreases severity of bionics installation failure
    int pl_skill = u->int_cur * 4 +
                   u->get_skill_level( skilll_electronics ) * 4 +
                   u->get_skill_level( skilll_firstaid )    * 3 +
                   u->get_skill_level( skilll_mechanics )   * 1;
    // Medical residents get a substantial assist here
    if( u->has_trait( trait_PROF_MED ) ) {
        pl_skill += 6;
    }

    // for failure_level calculation, shift skill down to a float between ~0.4 - 30
    float adjusted_skill = float ( pl_skill ) - std::min( float ( 40 ),
                           float ( pl_skill ) - float ( pl_skill ) / float ( 10.0 ) );

    // failure level is decided by how far off the character was from a successful install, and
    // this is scaled up or down by the ratio of difficulty/skill.  At high skill levels (or low
    // difficulties), only minor consequences occur.  At low skill levels, severe consequences
    // are more likely.
    int failure_level = int( sqrt( success * 4.0 * difficulty / float ( adjusted_skill ) ) );
    int fail_type = ( failure_level > 5 ? 5 : failure_level );

    if( fail_type <= 0 ) {
        add_msg( m_neutral, _( "The installation fails without incident." ) );
        return;
    }

    switch( rng( 1, 5 ) ) {
        case 1:
            add_msg( m_neutral, _( "You flub the installation." ) );
            break;
        case 2:
            add_msg( m_neutral, _( "You mess up the installation." ) );
            break;
        case 3:
            add_msg( m_neutral, _( "The installation fails." ) );
            break;
        case 4:
            add_msg( m_neutral, _( "The installation is a failure." ) );
            break;
        case 5:
            add_msg( m_neutral, _( "You screw up the installation." ) );
            break;
    }

    if( u->has_trait( trait_PROF_MED ) ) {
        //~"Complications" is USian medical-speak for "unintended damage from a medical procedure".
        add_msg( m_neutral, _( "Your training helps you minimize the complications." ) );
        // In addition to the bonus, medical residents know enough OR protocol to avoid botching.
        // Take MD and be immune to faulty bionics.
        if( fail_type == 5 ) {
            fail_type = rng( 1, 3 );
        }
    }

    if( fail_type == 3 && u->num_bionics() == 0 ) {
        fail_type = 2;    // If we have no bionics, take damage instead of losing some
    }

    switch( fail_type ) {

        case 1:
            if( !( u->has_trait( trait_id( "NOPAIN" ) ) ) ) {
                add_msg( m_bad, _( "It really hurts!" ) );
                u->mod_pain( rng( failure_level * 3, failure_level * 6 ) );
            }
            break;

        case 2:
            add_msg( m_bad, _( "Your body is damaged!" ) );
            u->hurtall( rng( failure_level, failure_level * 2 ), u ); // you hurt yourself
            break;

        case 3:
            if( u->num_bionics() <= failure_level && u->max_power_level == 0 ) {
                add_msg( m_bad, _( "All of your existing bionics are lost!" ) );
            } else {
                add_msg( m_bad, _( "Some of your existing bionics are lost!" ) );
            }
            for( int i = 0; i < failure_level && u->remove_random_bionic(); i++ ) {
                ;
            }
            break;

        case 4:
            add_msg( m_mixed, _( "Something went terribly wrong, you mutate!" ) );
            while( failure_level > 0 ) {
                u->mutate();
                failure_level -= rng( 1, failure_level + 2 );
            }
            break;

        case 5: {
            add_msg( m_bad, _( "The installation is faulty!" ) );
            std::vector<bionic_id> valid;
            std::copy_if( begin( faulty_bionics ), end( faulty_bionics ), std::back_inserter( valid ),
            [&]( bionic_id const & id ) {
                return !u->has_bionic( id );
            } );

            if( valid.empty() ) { // We've got all the bad bionics!
                if( u->max_power_level > 0 ) {
                    int old_power = u->max_power_level;
                    add_msg( m_bad, _( "You lose power capacity!" ) );
                    u->max_power_level = rng( 0, u->max_power_level - 25 );
                    u->add_memorial_log( pgettext( "memorial_male", "Lost %d units of power capacity." ),
                                         pgettext( "memorial_female", "Lost %d units of power capacity." ),
                                         old_power - u->max_power_level );
                }
                // TODO: What if we can't lose power capacity?  No penalty?
            } else {
                const bionic_id &id = random_entry( valid );
                u->add_bionic( id );
                u->add_memorial_log( pgettext( "memorial_male", "Installed bad bionic: %s." ),
                                     pgettext( "memorial_female", "Installed bad bionic: %s." ),
                                     bionics[ id ].name.c_str() );
            }
        }
        break;
    }
}

std::string list_occupied_bps( const bionic_id &bio_id, const std::string &intro,
                               const bool each_bp_on_new_line )
{
    if( bio_id->occupied_bodyparts.empty() ) {
        return "";
    }
    std::ostringstream desc;
    desc << intro;
    for( const auto &elem : bio_id->occupied_bodyparts ) {
        desc << ( each_bp_on_new_line ? "\n" : " " );
        //~ <Bodypart name> (<number of occupied slots> slots);
        desc << string_format( _( "%s (%i slots);" ),
                               body_part_name_as_heading( elem.first, 1 ).c_str(),
                               elem.second );
    }
    return desc.str();
}

int player::get_used_bionics_slots( const body_part bp ) const
{
    int used_slots = 0;
    for( auto &bio : *my_bionics ) {
        auto search = bionics[bio.id].occupied_bodyparts.find( bp );
        if( search != bionics[bio.id].occupied_bodyparts.end() ) {
            used_slots += search->second;
        }
    }

    return used_slots;
}

std::map<body_part, int> player::bionic_installation_issues( const bionic_id &bioid )
{
    std::map<body_part, int> issues;
    if( !has_trait( trait_id( "DEBUG_CBM_SLOTS" ) ) ) {
        return issues;
    }
    for( auto &elem : bioid->occupied_bodyparts ) {
        const int lacked_slots = elem.second - get_free_bionics_slots( elem.first );
        if( lacked_slots > 0 ) {
            issues.emplace( elem.first, lacked_slots );
        }
    }
    return issues;
}

int player::get_total_bionics_slots( const body_part bp ) const
{
    switch( bp ) {
        case bp_torso:
            return 80;

        case bp_head:
            return 18;

        case bp_eyes:
            return 4;

        case bp_mouth:
            return 4;

        case bp_arm_l:
        case bp_arm_r:
            return 20;

        case bp_hand_l:
        case bp_hand_r:
            return 5;

        case bp_leg_l:
        case bp_leg_r:
            return 30;

        case bp_foot_l:
        case bp_foot_r:
            return 7;

        case num_bp:
            debugmsg( "number of slots for incorrect bodypart is requested!" );
            return 0;
    }
    return 0;
}

int player::get_free_bionics_slots( const body_part bp ) const
{
    return get_total_bionics_slots( bp ) - get_used_bionics_slots( bp );
}

void player::add_bionic( bionic_id const &b )
{
    if( has_bionic( b ) ) {
        debugmsg( "Tried to install bionic %s that is already installed!", b.c_str() );
        return;
    }

    int pow_up = bionics[b].capacity;
    max_power_level += pow_up;
    if( b == "bio_power_storage" || b == "bio_power_storage_mkII" ) {
        add_msg_if_player( m_good, _( "Increased storage capacity by %i." ), pow_up );
        // Power Storage CBMs are not real bionic units, so return without adding it to my_bionics
        return;
    }

    my_bionics->push_back( bionic( b, get_free_invlet( *this ) ) );
    if( b == "bio_tools" || b == "bio_ears" ) {
        activate_bionic( my_bionics->size() - 1 );
    }

    for( const auto &inc_bid : bionics[b].included_bionics ) {
        add_bionic( inc_bid );
    }

    recalc_sight_limits();
}

void player::remove_bionic( bionic_id const &b )
{
    bionic_collection new_my_bionics;
    for( auto &i : *my_bionics ) {
        if( b == i.id ) {
            continue;
        }

        // Linked bionics: if either is removed, the other is removed as well.
        if( b->is_included( i.id ) || i.id->is_included( b ) ) {
            continue;
        }

        new_my_bionics.push_back( bionic( i.id, i.invlet ) );
    }
    *my_bionics = new_my_bionics;
    recalc_sight_limits();
}

int player::num_bionics() const
{
    return my_bionics->size();
}

std::pair<int, int> player::amount_of_storage_bionics() const
{
    int lvl = max_power_level;

    // exclude amount of power capacity obtained via non-power-storage CBMs
    for( auto it : *my_bionics ) {
        lvl -= bionics[it.id].capacity;
    }

    std::pair<int, int> results( 0, 0 );
    if( lvl <= 0 ) {
        return results;
    }

    int pow_mkI = bionics[bionic_id( "bio_power_storage" )].capacity;
    int pow_mkII = bionics[bionic_id( "bio_power_storage_mkII" )].capacity;

    while( lvl >= std::min( pow_mkI, pow_mkII ) ) {
        if( one_in( 2 ) ) {
            if( lvl >= pow_mkI ) {
                results.first++;
                lvl -= pow_mkI;
            }
        } else {
            if( lvl >= pow_mkII ) {
                results.second++;
                lvl -= pow_mkII;
            }
        }
    }
    return results;
}

bionic &player::bionic_at_index( int i )
{
    return ( *my_bionics )[i];
}



// Returns true if a bionic was removed.
bool player::remove_random_bionic()
{
    const int numb = num_bionics();
    if( numb ) {
        int rem = rng( 0, num_bionics() - 1 );
        const auto bionic = ( *my_bionics )[rem];
        remove_bionic( bionic.id );
        add_msg( m_bad, _( "Your %s fails, and is destroyed!" ), bionics[ bionic.id ].name.c_str() );
        recalc_sight_limits();
    }
    return numb;
}

void reset_bionics()
{
    bionics.clear();
    faulty_bionics.clear();
}

void load_bionic( JsonObject &jsobj )
{
    bionic_data new_bionic;

    const bionic_id id( jsobj.get_string( "id" ) );
    new_bionic.name = _( jsobj.get_string( "name" ).c_str() );
    new_bionic.description = _( jsobj.get_string( "description" ).c_str() );
    new_bionic.power_activate = jsobj.get_int( "act_cost", 0 );

    new_bionic.toggled = jsobj.get_bool( "toggled", false );
    // Requires ability to toggle
    new_bionic.power_deactivate = jsobj.get_int( "deact_cost", 0 );

    new_bionic.charge_time = jsobj.get_int( "time", 0 );
    // Requires a non-zero time
    new_bionic.power_over_time = jsobj.get_int( "react_cost", 0 );

    new_bionic.capacity = jsobj.get_int( "capacity", 0 );

    new_bionic.faulty = jsobj.get_bool( "faulty", false );
    new_bionic.power_source = jsobj.get_bool( "power_source", false );

    new_bionic.gun_bionic = jsobj.get_bool( "gun_bionic", false );
    new_bionic.weapon_bionic = jsobj.get_bool( "weapon_bionic", false );
    new_bionic.armor_interface = jsobj.get_bool( "armor_interface", false );

    if( new_bionic.gun_bionic && new_bionic.weapon_bionic ) {
        debugmsg( "Bionic %s specified as both gun and weapon bionic", id.c_str() );
    }

    new_bionic.fake_item = jsobj.get_string( "fake_item", "" );

    jsobj.read( "canceled_mutations", new_bionic.canceled_mutations );
    jsobj.read( "included_bionics", new_bionic.included_bionics );

    std::map<body_part, size_t> occupied_bodyparts;
    JsonArray jsarr = jsobj.get_array( "occupied_bodyparts" );
    if( !jsarr.empty() ) {
        while( jsarr.has_more() ) {
            JsonArray ja = jsarr.next_array();
            new_bionic.occupied_bodyparts.emplace( get_body_part_token( ja.get_string( 0 ) ),
                                                   ja.get_int( 1 ) );
        }
    }

    new_bionic.activated = new_bionic.toggled ||
                           new_bionic.power_activate > 0 ||
                           new_bionic.charge_time > 0;

    auto const result = bionics.insert( std::make_pair( id, new_bionic ) );

    if( !result.second ) {
        debugmsg( "duplicate bionic id" );
    } else if( new_bionic.faulty ) {
        faulty_bionics.push_back( id );
    }
}

void check_bionics()
{
    for( const auto &bio : bionics ) {
        if( !bio.second.fake_item.empty() &&
            !item::type_is_defined( bio.second.fake_item ) ) {
            debugmsg( "Bionic %s has unknown fake_item %s",
                      bio.first.c_str(), bio.second.fake_item.c_str() );
        }
        for( const auto &mid : bio.second.canceled_mutations ) {
            if( !mid.is_valid() ) {
                debugmsg( "Bionic %s cancels undefined mutation %s",
                          bio.first.c_str(), mid.c_str() );
            }
        }
        for( const auto &bid : bio.second.included_bionics ) {
            if( !bid.is_valid() ) {
                debugmsg( "Bionic %s includes undefined bionic %s",
                          bio.first.c_str(), bid.c_str() );
            }
            if( !bionics[bid].occupied_bodyparts.empty() ) {
                debugmsg( "Bionic %s (included by %s) consumes slots, those should be part of the containing bionic instead.",
                          bid.c_str(), bio.first.c_str() );
            }
        }
    }
}

int bionic::get_quality( const quality_id &q ) const
{
    const auto &i = info();
    if( i.fake_item.empty() ) {
        return INT_MIN;
    }

    return item( i.fake_item ).get_quality( q );
}

void bionic::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "id", id );
    json.member( "invlet", ( int )invlet );
    json.member( "powered", powered );
    json.member( "charge", charge );
    json.end_object();
}

void bionic::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    id = bionic_id( jo.get_string( "id" ) );
    invlet = jo.get_int( "invlet" );
    powered = jo.get_bool( "powered" );
    charge = jo.get_int( "charge" );
}
