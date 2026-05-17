#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include "../../Client/Module.hpp"

using namespace geode::prelude;

// itzz menu: ueberschreibt NUR das Cube-Icon mit dem rotierenden Stern.
// Ship/Ball/UFO/Wave/Robot/Spider/Swing behalten die vom User gewaehlten Icons.

class CubeIconOverride : public Module
{
    public:
        MODULE_SETUP(CubeIconOverride)
        {
            setName("Override Cube Icon");
            setID("cube-icon-override");
            // keine Kategorie -> nicht in der Modulliste; gesteuert ueber den Icon-Tab
            setDefaultEnabled(false);
        }
};

SUBMIT_HACK(CubeIconOverride);

class $modify(itzzCubeIconHook, PlayerObject)
{
    struct Fields
    {
        CCSprite* star = nullptr;
    };

    void update(float dt)
    {
        PlayerObject::update(dt);
        itzzUpdateCube();
    }

    void itzzUpdateCube()
    {
        bool isCube = !m_isShip && !m_isBall && !m_isBird && !m_isDart
                      && !m_isRobot && !m_isSpider && !m_isSwing;
        bool on = CubeIconOverride::get()->getRealEnabled() && isCube;

        if (on && !m_fields->star)
        {
            if (auto s = CCSprite::create("itzz-star.png"_spr))
            {
                s->setZOrder(1000);
                s->runAction(CCRepeatForever::create(CCRotateBy::create(1.5f, 360.f)));
                this->addChild(s);
                m_fields->star = s;
            }
        }

        if (m_fields->star)
        {
            m_fields->star->setVisible(on);

            if (on)
            {
                // Stern dynamisch auf Cube-Groesse skalieren.
                // Sichtbarer Stern ist nur ~30px im 136px-Canvas -> entsprechend hochskalieren.
                float cubeW = 30.f;
                if (m_iconSprite)
                    cubeW = m_iconSprite->getContentWidth() * m_iconSprite->getScaleX();

                float opaqueStarPx = 30.f; // gemessener opaker Bereich im Canvas
                m_fields->star->setScale((cubeW / opaqueStarPx) * 1.1f);

                if (m_iconSprite)
                    m_fields->star->setPosition(m_iconSprite->getPosition());
            }
        }

        bool vis = !on;
        if (m_iconSprite) m_iconSprite->setVisible(vis);
        if (m_iconSpriteSecondary) m_iconSpriteSecondary->setVisible(vis);
        if (m_iconGlow) m_iconGlow->setVisible(vis);
    }
};
