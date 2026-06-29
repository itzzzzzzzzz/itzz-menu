#include "../../Client/Module.hpp"
#include "../Utils/PlayLayer.hpp"
#include "ShowTrajectory/TrajectoryNode.hpp"
#include "CheckpointFix/PlayerState.hpp"
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <vector>
#include <utility>
#include <algorithm>

using namespace geode::prelude;
using namespace qolmod;

// itzz menu: Autoplay (beta)
// Vorausschauender Planer auf Basis der Show-Trajectory-Simulation.
// - Tap-Modi (Cube/Ball/Spider): rekursive Mehrstufen-Suche ueber Sprung-/Orb-Tap-Sequenzen.
//   Orbs werden im Klon DETERMINISTISCH ueber den echten ringJump gefeuert.
// - Hold-Modi (Ship/Wave/Robot/...): Halten vs. Loslassen mit Hysterese (pro Frame).
// Es wird der spaetest-noetige Eingriff gewaehlt -> nur springen/tappen wenn noetig.
// Der gewaehlte Pfad wird magenta visualisiert.

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

static const int AP_H       = 160; // Lookahead in Sim-Schritten
static const int AP_DEPTH   = 3;   // Suchtiefe (Anzahl Eingriffe in Folge)
static const int AP_MAXCAND = 10;  // max. gepruefte Eingriffs-Zeitpunkte pro Ebene
static const int AP_HYST    = 8;   // Hysterese (Hold-Modi)

static bool apIsHoldMode(PlayerObject* p)
{
    return p->m_isShip || p->m_isBird || p->m_isDart || p->m_isSwing || p->m_isRobot;
}

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

// Fuehrt am aktuellen Klon-Zustand den Eingriff aus: Orb feuern bzw. Sprung druecken,
// dann genau EINEN Schritt. true = noch am Leben.
static bool apIntervene(TrajectoryNode* tn, RingObject* ring)
{
    if (ring)
    {
        tn->apClone()->ringJump(ring, false); // deterministische, echte Orb-Physik
        tn->apHold(false);
    }
    else
    {
        tn->apHold(true);
    }
    return tn->apStep();
}

// Rekursive Suche (Tap-Modi). Klon-Zustand muss vom Aufrufer gesetzt sein.
// Gibt die Zahl ueberlebter Schritte zurueck; outFirstT = Frame des ersten Eingriffs (-1 = keiner).
static int apSearchTap(TrajectoryNode* tn, int horizon, int depth, int& outFirstT)
{
    outFirstT = -1;

    PlayerState base;
    base.saveState(tn->apClone());

    // Release-Rollout: Kandidaten (Boden/Orb) sammeln, Todeszeitpunkt finden
    std::vector<std::pair<int, RingObject*>> cand;
    int deathRel = horizon;
    for (int t = 0; t < horizon; t++)
    {
        bool ground = tn->apClone()->m_isOnGround;
        tn->apHold(false);
        bool alive = tn->apStep();
        RingObject* orb = tn->apOrbThisStep;

        if (orb)        cand.push_back({ t, orb });
        else if (ground) cand.push_back({ t, nullptr });

        if (!alive) { deathRel = t; break; }
    }

    if (deathRel >= horizon)
        return horizon; // Release ist sicher -> kein Eingriff noetig

    int best = deathRel;
    int startIdx = std::max(0, (int)cand.size() - AP_MAXCAND);

    for (int i = (int)cand.size() - 1; i >= startIdx; i--)
    {
        int t = cand[i].first;
        RingObject* ring = cand[i].second;

        base.loadState(tn->apClone());

        bool ok = true;
        for (int k = 0; k < t; k++)
        {
            tn->apHold(false);
            if (!tn->apStep()) { ok = false; break; }
        }
        if (!ok)
            continue;

        if (!apIntervene(tn, ring))
            continue; // Eingriff sofort toedlich

        int after = t + 1;
        int reach;
        if (depth <= 1)
        {
            reach = after + apRollout(tn, false, horizon - after);
        }
        else
        {
            int subT;
            reach = after + apSearchTap(tn, horizon - after, depth - 1, subT);
        }

        if (reach > best) { best = reach; outFirstT = t; }
        if (best >= horizon) break;
    }

    return best;
}

// Entscheidung: in diesem Frame druecken?
static bool apDecide(PlayerObject* plr, bool holdingNow, bool recordViz)
{
    auto tn = TrajectoryNode::get();
    if (!tn || !plr || plr->m_isDead)
        return false;

    tn->apBegin();
    PlayerState s0;
    tn->apLoadFrom(plr);
    s0.saveState(tn->apClone());

    bool press = false;
    int  vizFirstT = -1;
    bool vizRing = false;

    if (!apIsHoldMode(plr))
    {
        // --- Tap-Modi: rekursive Sequenz-Suche ---
        int firstT;
        int reach = apSearchTap(tn, AP_H, AP_DEPTH, firstT);
        (void)reach;
        press = (firstT == 0);
        vizFirstT = firstT;

        // fuer die Visualisierung: war der erste Eingriff ein Orb?
        // (firstT-Kandidat erneut bestimmen ist teuer; wir markieren generisch)
    }
    else
    {
        // --- Hold-Modi: Halten vs. Loslassen mit Hysterese ---
        int reachRel = apRollout(tn, false, AP_H);
        if (reachRel < AP_H - (AP_H / 12))
        {
            s0.loadState(tn->apClone());
            int reachHold = apRollout(tn, true, AP_H);
            int margin = holdingNow ? -AP_HYST : AP_HYST;
            press = reachHold > reachRel + margin;
        }
        vizFirstT = press ? 0 : -1;
    }

    // --- Visualisierung des gewaehlten ersten Schritts ---
    if (recordViz)
    {
        tn->apPath.clear();
        s0.loadState(tn->apClone());
        tn->apPath.push_back(tn->apClone()->getPosition());

        bool holdMode = apIsHoldMode(plr);
        for (int t = 0; t < AP_H; t++)
        {
            bool h;
            if (holdMode)
                h = press; // Hold-Modus: Eingabe konstant darstellen
            else
                h = (t == vizFirstT); // Tap-Modus: ein Druck am gewaehlten Frame

            tn->apHold(h);
            if (!tn->apStep())
                break;
            tn->apPath.push_back(tn->apClone()->getPosition());
        }
        (void)vizRing;
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
