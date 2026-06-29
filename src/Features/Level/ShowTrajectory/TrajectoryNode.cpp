#include "TrajectoryNode.hpp"
#include "../CheckpointFix/PlayerState.hpp"
#include "ShowTrajectory.hpp"

using namespace geode::prelude;
using namespace qolmod;

TrajectoryNode* TrajectoryNode::get()
{
    return instance;
}

TrajectoryNode::~TrajectoryNode()
{
    instance = nullptr;
}

bool TrajectoryNode::init()
{
    if (!BaseDrawNode::init())
        return false;

    instance = this;

    player = PlayerObject::create(0, 0, gjbgl, gjbgl->m_objectLayer, false);
    player->setVisible(false);
    gjbgl->m_objectLayer->addChild(player);
    trueCheck = CCNode::create();

    setDeltaIter(0.5f);
    setIterCount(240);

    return true;
}

bool TrajectoryNode::isSimulating()
{
    return simulating;
}

float TrajectoryNode::getDeltaIter()
{
    return deltaIter;
}

int TrajectoryNode::getIterCount()
{
    return iterCount;
}

void TrajectoryNode::setDeltaIter(float delta)
{
    this->deltaIter = delta;
}

void TrajectoryNode::setIterCount(int iters)
{
    this->iterCount = iters;
}

void TrajectoryNode::redraw()
{
    clear();

    // itzz Autoplay: geplanten Pfad zeichnen (magenta), unabhaengig von Show Trajectory
    if (apDrawPath && apPath.size() >= 2)
    {
        for (size_t i = 1; i < apPath.size(); i++)
            drawSegment(apPath[i - 1], apPath[i], 1.3f, ccc4f(0.96f, 0.18f, 0.86f, 1.f));
    }

    if (!ShowTrajectory::get()->getRealEnabled())
        return;

    if (ShowTrajectoryP1::get()->getRealEnabled())
    {
        simulate(gjbgl->m_player1, false);
        simulate(gjbgl->m_player1, true);
    }
    
    if (ShowTrajectoryP2::get()->getRealEnabled())
    {
        if (gjbgl->m_player2 && gjbgl->m_gameState.m_isDualMode)
        {
            simulate(gjbgl->m_player2, false);
            simulate(gjbgl->m_player2, true);
        }
    }
}

void TrajectoryNode::simulate(PlayerObject* plr, bool held)
{
    if (plr->m_isDead)
        return;

    simulatedRings.clear();
    simulating = true;
    ringSimulated = !held;

    PlayerState state;
    state.saveState(plr);
    state.loadState(this->player);

    if (gjbgl->m_playbackMode != PlaybackMode::Playing && gjbgl->m_isEditor)
    {
        player->m_isShip = gjbgl->m_levelSettings->m_startMode == 1;
        player->m_isBall = gjbgl->m_levelSettings->m_startMode == 2;
        player->m_isBird = gjbgl->m_levelSettings->m_startMode == 3;
        player->m_isDart = gjbgl->m_levelSettings->m_startMode == 4;
        player->m_isRobot = gjbgl->m_levelSettings->m_startMode == 5;
        player->m_isSpider = gjbgl->m_levelSettings->m_startMode == 6;
        player->m_isSwing = gjbgl->m_levelSettings->m_startMode == 7;
    }

    if (
        (held && state.plMembers.m_dashRing) ||
        (held && state.plMembers.m_isRobot && state.plMembers.m_holdingButtons[(int)PlayerButton::Jump])
    ) {}
    else
    {
        player->releaseButton(PlayerButton::Jump);
        if (held)
            player->pushButton(PlayerButton::Jump);
    }

    bool useHoldCol = held == plr->m_holdingButtons[(int)PlayerButton::Jump];
    ccColor4F col;
    bool useTrail = false;
    
    if (plr == gjbgl->m_player1)
    {
        col = ccc4FFromccc3B(useHoldCol ?
            ShowTrajectoryP1Hold::get()->getColour() :
            ShowTrajectoryP1Release::get()->getColour());
        useTrail = ShowTrajectoryP1Trail::get()->getRealEnabled();
    }
    else
    {
        col = ccc4FFromccc3B(useHoldCol ?
            ShowTrajectoryP2Hold::get()->getColour() :
            ShowTrajectoryP2Release::get()->getColour());
        useTrail = ShowTrajectoryP2Trail::get()->getRealEnabled();
    }

    if (player->m_regularTrail)
        player->m_regularTrail->setVisible(false);

    performSimulation(col, useTrail, false);

    simulating = false;
}

int TrajectoryNode::simulateSurvival(PlayerObject* plr, bool held, int steps)
{
    if (!plr || plr->m_isDead)
        return 0;

    if (steps <= 0)
        steps = getIterCount();

    simulating = true;
    ringSimulated = !held;
    simulatedRings.clear();

    PlayerState state;
    state.saveState(plr);
    state.loadState(this->player);

    // Sprung-Input setzen (wie in simulate())
    if (
        (held && state.plMembers.m_dashRing) ||
        (held && state.plMembers.m_isRobot && state.plMembers.m_holdingButtons[(int)PlayerButton::Jump])
    ) {}
    else
    {
        player->releaseButton(PlayerButton::Jump);
        if (held)
            player->pushButton(PlayerButton::Jump);
    }

    int survived = steps;
    for (int i = 0; i < steps; i++)
    {
        player->m_collisionLogTop->removeAllObjects();
        player->m_collisionLogBottom->removeAllObjects();
        player->m_collisionLogLeft->removeAllObjects();
        player->m_collisionLogRight->removeAllObjects();

        player->update(deltaIter);
        gjbgl->checkCollisions(player, deltaIter, false);
        player->updateRotation(deltaIter);
        player->updatePlayerScale();

        if (player->m_isDead)
        {
            survived = i;
            break;
        }
    }

    simulating = false;
    return survived;
}

void TrajectoryNode::apLoadFrom(PlayerObject* from)
{
    PlayerState s;
    s.saveState(from);
    s.loadState(this->player);
}

void TrajectoryNode::apHold(bool held)
{
    // Dash-Ring haelt sich selbst -> nicht neu druecken
    if (held && this->player->m_dashRing)
        return;

    this->player->releaseButton(PlayerButton::Jump);
    if (held)
        this->player->pushButton(PlayerButton::Jump);
}

bool TrajectoryNode::apStep()
{
    apOrbThisStep = nullptr; // wird im playerTouchedRing-Hook gesetzt, falls ein Orb beruehrt wird

    player->m_collisionLogTop->removeAllObjects();
    player->m_collisionLogBottom->removeAllObjects();
    player->m_collisionLogLeft->removeAllObjects();
    player->m_collisionLogRight->removeAllObjects();

    player->update(deltaIter);
    gjbgl->checkCollisions(player, deltaIter, false);
    player->updateRotation(deltaIter);
    player->updatePlayerScale();

    return !player->m_isDead;
}

void TrajectoryNode::simulateFromRing(PlayerObject* player2, RingObject* ring)
{
    return;

    if (ringSimulated)
        return;

    if (std::find(simulatedRings.begin(), simulatedRings.end(), ring) != simulatedRings.end())
        return;

    simulatedRings.push_back(ring);

    PlayerState state;
    state.saveState(player);

    player->setUserObject("ring-touched", nullptr);

    float yellow = 11.18f;

    switch (ring->m_objectType)
    {
        default:
            return;

        case GameObjectType::YellowJumpRing:
            if (player->m_isShip)
                player->m_yVelocity = 8;
            else if (player->m_isBall)
                player->m_yVelocity = yellow * 0.7f;
            else if (player->m_isBird)
                player->m_yVelocity = 8;
            else if (player->m_isDart)
                return;
            else if (player->m_isRobot)
                player->m_yVelocity = yellow * 0.9f;
            else if (player->m_isSpider)
                player->m_yVelocity = yellow * 0.7f;
            else if (player->m_isSwing)
                player->m_yVelocity = yellow * 0.6f;
            else
                player->m_yVelocity = yellow;

            break;
    }

    performSimulation(ccc4f(1, 1, 0, 1), false, true);

    state.loadState(this->player);
}

void TrajectoryNode::performSimulation(cocos2d::ccColor4F colour, bool useTrail, bool isOrb)
{
    CCPoint prevPoint = player->getPosition();
    auto oldTrails = trailStates;
    bool drewTrails = false;
    existingSimulationCancelled = false;

    for (size_t i = 0; i < getIterCount(); i++)
    {
        player->m_collisionLogTop->removeAllObjects();
        player->m_collisionLogBottom->removeAllObjects();
        player->m_collisionLogLeft->removeAllObjects();
        player->m_collisionLogRight->removeAllObjects();

        player->update(deltaIter);
        gjbgl->checkCollisions(player, deltaIter, false);

        player->updateRotation(deltaIter);
        player->updatePlayerScale();

        drawSegment(prevPoint, player->getPosition(), 1, colour);
        prevPoint = player->getPosition();

        if (player->m_isDead || player->m_maybeIsColliding)
        {
            drewTrails = true;
            drawPlayerTrails();
            drawPlayerHitbox(player);
            break;
        }
        if (useTrail)
        {
            trailStates.push_back(PlayerHitboxState{
                player->getObjectRect(player->m_vehicleSize, player->m_vehicleSize),
                player->getObjectRect(0.25f, 0.25f),
                0
            });
        }

        if (existingSimulationCancelled)
            break;
    }

    if (!drewTrails)
        drawPlayerTrails();

    trailStates = oldTrails;
    existingSimulationCancelled = false;
}

bool TrajectoryNode::shouldDrawTrail()
{
    return true;
}