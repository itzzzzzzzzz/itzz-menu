#pragma once

#include <Geode/Geode.hpp>
#include "../Hitboxes/HitboxNode.hpp"

namespace qolmod
{
    class TrajectoryNode : public HitboxNode
    {
        protected:
            static inline TrajectoryNode* instance = nullptr;
            PlayerObject* player = nullptr;
            bool simulating = false;
            bool autoplaySim = false;
            float deltaIter = 0.5f;
            int iterCount = 240;
            bool ringSimulated = false;
            std::vector<GameObject*> simulatedRings = {};
            bool existingSimulationCancelled = false;
            geode::Ref<cocos2d::CCNode> trueCheck = nullptr;

            ~TrajectoryNode();
            
            virtual bool shouldDrawTrail();

        public:
            CREATE_FUNC(TrajectoryNode);
            static TrajectoryNode* get();

            bool isSimulating();
            bool isAutoplaySim() { return autoplaySim; }
            void simulate(PlayerObject* plr, bool held);
            // itzz Autoplay: simuliert plr 'steps' Schritte mit gehaltenem/losgelassenem Sprung
            // und liefert, wie viele Schritte ueberlebt werden (steps = ganzer Horizont ueberlebt).
            int simulateSurvival(PlayerObject* plr, bool held, int steps = 0);

            // itzz Autoplay-Primitiven: der Planer steuert den versteckten Klon schrittweise,
            // um mehrere Sprung-Zeitpunkte/Pfade durchzurechnen.
            // Im autoplaySim-Modus feuern Orbs ueber den echten Spielcode (exakte Physik).
            PlayerObject* apClone() { return player; }
            void apBegin() { simulating = true; autoplaySim = true; }
            void apEnd()   { simulating = false; autoplaySim = false; apOrbThisStep = nullptr; }
            void apLoadFrom(PlayerObject* from); // realen Spielerzustand in den Klon laden
            void apHold(bool held);              // Sprungtaste am Klon setzen
            bool apStep();                       // ein Physikschritt; true = noch am Leben

            // Orb, den der Klon im aktuellen Schritt beruehrt hat (vom playerTouchedRing-Hook gesetzt)
            RingObject* apOrbThisStep = nullptr;
            // Visualisierung des geplanten Pfads
            std::vector<cocos2d::CCPoint> apPath = {};
            bool apDrawPath = false;

            void simulateFromRing(PlayerObject* player, RingObject* ring);
            void performSimulation(cocos2d::ccColor4F colour, bool useTrail, bool isOrb);

            float getDeltaIter();
            int getIterCount();

            void setDeltaIter(float delta);
            void setIterCount(int iters);

            virtual void redraw();
            virtual bool init();
    };
}