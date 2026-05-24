#include "../../Client/Module.hpp"
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/MenuGameLayer.hpp>

using namespace geode::prelude;

// itzz: animierter GLSL-Shader-Hintergrund im GD-Hauptmenue.
// three.js/paper-shaders laeuft nicht in GD (Web/WebGL); hier als cocos2d-GLSL nachgebaut.
// uv wird aus gl_FragCoord/u_resolution berechnet -> fuellt den Screen unabhaengig von der Sprite-Groesse.

static const char* ITZZ_VERT = R"(
attribute vec4 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;
void main() {
    gl_Position = CC_MVPMatrix * a_position;
    v_texCoord = a_texCoord;
}
)";

static const char* ITZZ_FRAG = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform float u_time;
uniform vec2 u_resolution;
varying vec2 v_texCoord;

const float DIST = 0.8;   // u_distortion
const float SWIRL = 0.2;  // u_swirl

float hash21(vec2 p) {
    p = fract(p * vec2(0.3183099, 0.3678794)) + 0.1;
    p += dot(p, p + 19.19);
    return fract(p.x * p.y);
}
vec2 rotate(vec2 uv, float th) {
    return mat2(cos(th), sin(th), -sin(th), cos(th)) * uv;
}
vec2 getPosition(float i, float t) {
    float a = i * 0.37;
    float b = 0.6 + fract(i / 3.0) * 0.9;
    float c = 0.8 + fract((i + 1.0) / 4.0);
    float x = sin(t * b + a);
    float y = cos(t * c + a * 1.5);
    return 0.5 + 0.5 * vec2(x, y);
}
float spotWeight(vec2 uvR, float i, float t) {
    vec2 p = getPosition(i, t);
    float d = pow(length(uvR - p), 3.5);
    return 1.0 / (d + 1e-3);
}

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution;

    float t = 0.5 * (u_time + 41.5);

    float radius = smoothstep(0.0, 1.0, length(uv - 0.5));
    float center = 1.0 - radius;

    // organische Distortion (2 Iterationen, entrollt)
    uv.x += DIST * center * sin(t + 0.4 * smoothstep(0.0, 1.0, uv.y)) * cos(0.2 * t + 2.4 * smoothstep(0.0, 1.0, uv.y));
    uv.y += DIST * center * cos(t + 2.0 * smoothstep(0.0, 1.0, uv.x));
    uv.x += DIST * center / 2.0 * sin(t + 0.8 * smoothstep(0.0, 1.0, uv.y)) * cos(0.2 * t + 4.8 * smoothstep(0.0, 1.0, uv.y));
    uv.y += DIST * center / 2.0 * cos(t + 4.0 * smoothstep(0.0, 1.0, uv.x));

    vec2 uvR = uv - vec2(0.5);
    float angle = 3.0 * SWIRL * radius;
    uvR = rotate(uvR, -angle);
    uvR += vec2(0.5);

    // 4 Farb-Spots: #000000, #1a1a1a, #333333, #ffffff
    float w0 = spotWeight(uvR, 0.0, t);
    float w1 = spotWeight(uvR, 1.0, t);
    float w2 = spotWeight(uvR, 2.0, t);
    float w3 = spotWeight(uvR, 3.0, t);

    vec3 col = vec3(0.0) * w0 + vec3(0.102) * w1 + vec3(0.2) * w2 + vec3(1.0) * w3;
    float tw = w0 + w1 + w2 + w3;
    col /= max(1e-4, tw);

    gl_FragColor = vec4(col, 1.0);
}
)";

class ItzzShaderBG : public CCNode
{
    public:
        CCGLProgram* m_prog = nullptr;
        GLint m_timeLoc = -1;
        GLint m_resLoc = -1;
        float m_t = 0.f;
        float m_resW = 1.f;
        float m_resH = 1.f;

        static ItzzShaderBG* create()
        {
            auto p = new ItzzShaderBG();
            if (p && p->initBG())
            {
                p->autorelease();
                return p;
            }
            CC_SAFE_DELETE(p);
            return nullptr;
        }

        bool initBG()
        {
            if (!CCNode::init())
                return false;

            auto spr = CCSprite::create("itzz-bg.png"_spr);
            if (!spr)
                return false;

            auto win = CCDirector::get()->getWinSize();
            auto px = CCDirector::get()->getOpenGLView()->getFrameSize();
            m_resW = px.width;
            m_resH = px.height;

            // Sprite muss nur ALLE Pixel abdecken -> grosszuegig ueberskalieren, zentriert.
            // (uv kommt aus gl_FragCoord, daher ist die Sprite-Groesse fuer das Muster egal.)
            spr->setAnchorPoint(ccp(0.5f, 0.5f));
            spr->setPosition(ccp(win.width / 2.f, win.height / 2.f));
            auto cs = spr->getContentSize();
            float sc = std::max(win.width / cs.width, win.height / cs.height) * 3.0f;
            spr->setScale(sc);

            auto prog = new CCGLProgram();
            if (prog->initWithVertexShaderByteArray(ITZZ_VERT, ITZZ_FRAG))
            {
                prog->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
                prog->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
                prog->link();
                prog->updateUniforms();
                spr->setShaderProgram(prog);
                m_prog = prog;
                m_timeLoc = prog->getUniformLocationForName("u_time");
                m_resLoc = prog->getUniformLocationForName("u_resolution");
            }
            else
            {
                spr->setColor(ccc3(60, 22, 10)); // sichtbarer Fallback
            }
            prog->release();

            this->addChild(spr);
            scheduleUpdate();
            return true;
        }

        void update(float dt)
        {
            m_t += dt;
            if (m_prog)
            {
                m_prog->use();
                if (m_timeLoc >= 0)
                    m_prog->setUniformLocationWith1f(m_timeLoc, m_t);
                if (m_resLoc >= 0)
                    m_prog->setUniformLocationWith2f(m_resLoc, m_resW, m_resH);
            }
        }
};

class ShaderMenuBG : public Module
{
    public:
        MODULE_SETUP(ShaderMenuBG)
        {
            setName("Shader Background");
            setID("shader-menu-bg");
            setCategory("Universal");
            setDescription("Animated shader background on the main menu");
            setDefaultEnabled(true);
        }
};

SUBMIT_HACK(ShaderMenuBG);

class $modify(itzzShaderMenu, MenuLayer)
{
    bool init()
    {
        if (!MenuLayer::init())
            return false;

        if (ShaderMenuBG::get()->getRealEnabled())
        {
            if (auto gameBg = this->getChildByType<MenuGameLayer>(0))
                gameBg->setVisible(false);

            if (auto bg = ItzzShaderBG::create())
                this->addChild(bg, -10);
        }

        return true;
    }
};
