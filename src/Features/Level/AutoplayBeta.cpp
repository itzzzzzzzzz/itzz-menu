#include "../../Client/Module.hpp"
#include "../Utils/PlayLayer.hpp"
#include "ShowTrajectory/TrajectoryNode.hpp"
#include "CheckpointFix/PlayerState.hpp"
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <vector>
#include <algorithm>

using namespace geode::prelude;
using namespace qolmod;

// itzz menu: Autoplay (beta)
// Vorausschauender Planer auf Basis der Show-Trajectory-Simulation. Pro Frame werden
// mehrere Pfade durchgerechnet: nichts tun (Release), durchhalten (Hold) und jeder
// moegliche Sprung-/Orb-Tap-Zeitpunkt. Gewaehlt wird der SPAETEST-noetige Eingriff
// -> springt/tappt nur, wenn es ohne in den Tod ginge. Bloecke, Spikes, Pads, Portale
// UND Orbs (echte Spielphysik im Klon) werden beruecksichtigt. Der gewaehlte Pfad
// wird live magenta visualisiert.

class AutoplayBeta : public Module
{
    public:
        MODULE_SETUP(AutoplayBeta)
        {
            setName("Autoplay (beta)");
            setID("autoplay-beta");
            setCategory("Autoplay");
            setSafeModeTrigger(SafeModeTrigger::Attempt);
        }
};

SUBMIT_HACK(AutoplayBeta);

static const int AP_H       = 150; // Lookahead in Sim-Schritten
static const int AP_SAFE    = 140; // gilt als "sicher", wenn so lange ueberlebt wird
static const int AP_MAXCAND = 16;  // max. gepruefte Eingriffs-Zeitpunkte (die spaetesten)
static const int AP_HYST    = 8;   // Hysterese gegen Flackern

static bool apIsHoldMode(PlayerObject* p)
{
    return p->m_isShip || p->m_isBird || p->m_isDart || p->m_isSwing || p->m_isRobot;
}

// Rollt vom aktuellen Klon-Zustand los und liefert die Zahl ueberlebter Schritte.
static int apRollout(TrajectoryNode* tn, bool hold, int steps)
{
    for (int i = 0; i < steps; i++)
    {
        tn->apHold(hold);
        if (!tn->apStep())
            return i;
    }
    return steps;
}

// Entscheidung: Sprung/Tap in diesem Frame druecken?  recordViz = Pfad fuer die Anzeige merken.
static bool apDecide(PlayerObject* plr, bool holdingNow, bool recordViz)
{
    auto tn = TrajectoryNode::get();
    if (!tn || !plr || plr->m_isDead)
        return false;

    bool tapMode = !apIsHoldMode(plr);

    tn->apBegin();
    PlayerState s0;
    tn->apLoadFrom(plr);
    s0.saveState(tn->apClone());

    // --- Policy A: Release ueber den Horizont, Eingriffs-Kandidaten sammeln ---
    std::vector<int> cand; // Frames mit Boden (Tap-Modus) oder Orb-Kontakt
    int reachRel = AP_H;
    for (int t = 0; t < AP_H; t++)
    {
        bool ground = tapMode && tn->apClone()->m_isOnGround;
        tn->apHold(false);
        bool alive = tn->apStep();
        bool orb = (tn->apOrbThisStep != nullptr);
        if (ground || orb)
            cand.push_back(t);
        if (!alive) { reachRel = t; break; }
    }

    // --- Policy B: Hold ueber den Horizont ---
    s0.loadState(tn->apClone());
    int reachHold = apRollout(tn, true, AP_H);

    // --- Policy C: spaetest-noetiger Einzel-Eingriff (Sprung/Orb-Tap) ---
    int bestPressReach = reachHold;
    int bestTapT = -1; // -1 = Hold-Politik
    if (reachRel < AP_SAFE)
    {
        int start = std::max(0, (int)cand.size() - AP_MAXCAND);
        for (int i = (int)cand.size() - 1; i >= start; i--)
        {
            int t = cand[i];
            s0.loadState(tn->apClone());

            bool ok = true;
            for (int k = 0; k < t; k++)
            {
                tn->apHold(false);
                if (!tn->apStep()) { ok = false; break; }
            }
            if (!ok)
                continue;

            tn->apHold(true);            // an t druecken (Sprung bzw. Orb feuert)
            if (!tn->apStep())
                continue;

            int reach = t + 1 + apRollout(tn, false, AP_H - t - 1);
            if (reach > bestPressReach) { bestPressReach = reach; bestTapT = t; }
            if (bestPressReach >= AP_SAFE) break;
        }
    }

    // --- Entscheidung ---
    int  vizPlan = 0; // 0 release, 1 hold, 2 tap
    int  vizT    = 0;
    bool press   = false;

    if (reachRel >= AP_SAFE)
    {
        press = false; // nichts tun reicht -> NICHT eingreifen
    }
    else
    {
        int margin = holdingNow ? -AP_HYST : AP_HYST;
        if (bestPressReach > reachRel + margin)
        {
            if (bestTapT < 0) { press = true;            vizPlan = 1; }            // Hold
            else              { press = (bestTapT == 0); vizPlan = 2; vizT = bestTapT; } // Tap
        }
    }

    // --- Visualisierung des gewaehlten Plans ---
    if (recordViz)
    {
        tn->apPath.clear();
        s0.loadState(tn->apClone());
        tn->apPath.push_back(tn->apClone()->getPosition());
        for (int t = 0; t < AP_H; t++)
        {
            bool h = (vizPlan == 1) ? true : (vizPlan == 2 ? (t == vizT) : false);
            tn->apHold(h);
            if (!tn->apStep())
                break;
            tn->apPath.push_back(tn->apClone()->getPosition());
        }
    }

    tn->apEnd();
    return press;
}

class $modify(AutoplayBaseGameLayer, GJBaseGameLayer)
{
    struct Fields
    {
        bool p1State = false;
        bool p2State = false;
    };

    void applyAutoplay(PlayerObject* plr, bool isPlayer1, bool& state, bool recordViz)
    {
        if (!plr || plr->m_isDead)
            return;

        bool want = apDecide(plr, state, recordViz);
        if (want != state)
        {
            this->GJBaseGameLayer::handleButton(want, (int)PlayerButton::Jump, isPlayer1);
            state = want;
        }
    }

    #if GEODE_COMP_GD_VERSION >= 22081
    void processQueuedButtons(float dt, bool clearInputQueue)
    #else
    void processQueuedButtons()
    #endif
    {
        auto pl = PlayLayer::get();
        auto tn = TrajectoryNode::get();
        bool on = AutoplayBeta::get()->getRealEnabled() && pl && !pl->m_isPaused;

        if (tn)
            tn->apDrawPath = on;

        if (on)
        {
            auto fields = m_fields.self();
            applyAutoplay(m_player1, true, fields->p1State, true);

            if (m_player2 && m_gameState.m_isDualMode)
                applyAutoplay(m_player2, false, fields->p2State, false);
        }

        #if GEODE_COMP_GD_VERSION >= 22081
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
        #else
        GJBaseGameLayer::processQueuedButtons();
        #endif
    }

    void resetLevelVariables()
    {
        GJBaseGameLayer::resetLevelVariables();

        m_fields->p1State = false;
        m_fields->p2State = false;
    }
};
