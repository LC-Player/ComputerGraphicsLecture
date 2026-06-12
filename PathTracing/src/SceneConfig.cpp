// SceneConfig.cpp

#include "SceneConfig.h"
#include "tinyxml2.h"
#include <glm/gtc/constants.hpp>
#include <set>
#include <cmath>

namespace RYRayTracing {

namespace {

float parseFloatAttr(const char* s)
{
    if (!s || !*s) return 0.0f;
    std::string str(s);
    // Strip trailing 'f' (e.g. "0.1f")
    if (!str.empty() && (str.back() == 'f' || str.back() == 'F'))
        str.pop_back();
    return std::stof(str);
}

glm::vec3 parseVec3Child(tinyxml2::XMLElement* parent, const char* name)
{
    auto* el = parent->FirstChildElement(name);
    if (!el) return {0.0f, 0.0f, 0.0f};
    return {
        parseFloatAttr(el->Attribute("x")),
        parseFloatAttr(el->Attribute("y")),
        parseFloatAttr(el->Attribute("z"))
    };
}

glm::vec4 parseVec4Child(tinyxml2::XMLElement* parent, const char* name)
{
    auto* el = parent->FirstChildElement(name);
    if (!el) return {0.0f, 0.0f, 0.0f, 0.0f};
    return {
        parseFloatAttr(el->Attribute("x")),
        parseFloatAttr(el->Attribute("y")),
        parseFloatAttr(el->Attribute("z")),
        parseFloatAttr(el->Attribute("w"))
    };
}

glm::vec3 parseColorChild(tinyxml2::XMLElement* parent, const char* name)
{
    auto* el = parent->FirstChildElement(name);
    if (!el) return {1.0f, 1.0f, 1.0f};
    return {
        parseFloatAttr(el->Attribute("r")),
        parseFloatAttr(el->Attribute("g")),
        parseFloatAttr(el->Attribute("b"))
    };
}

const std::set<std::string> kModelTagNames = {
    "Duck", "Asschercut", "Bunny", "Dragon", "Venus", "FudanLogo"
};

void parseModelElement(tinyxml2::XMLElement* el, ParsedModel& out)
{
    auto* args = el->FirstChildElement("Args");
    if (args) {
        const char* fn = args->Attribute("filename");
        if (fn) out.filename = fn;
        out.display = args->IntAttribute("display", 1) != 0;
        out.normalInterpolation = args->IntAttribute("normalinterpolation", 0) != 0;
    }

    out.scale = parseVec3Child(el, "Scale");
    out.rotation = parseVec3Child(el, "Rotation");
    out.translation = parseVec3Child(el, "Translation");

    auto* iorEl = el->FirstChildElement("RefractiveIndex");
    if (iorEl)
        out.ior = parseFloatAttr(iorEl->Attribute("x"));

    out.albedo = parseVec4Child(el, "Albedo");
    out.diffuseColor = parseColorChild(el, "Diffuse");

    auto* shininessEl = el->FirstChildElement("Shiness");
    if (shininessEl)
        out.shininess = parseFloatAttr(shininessEl->Attribute("p"));

    convertToPBR(out);
}

void parsePointLight(tinyxml2::XMLElement* el, ParsedLight& out)
{
    out.type = LightType::Point;
    out.position = parseVec3Child(el, "Position");
    out.color = parseColorChild(el, "Color");
    auto* args = el->FirstChildElement("Args");
    if (args) {
        out.intensity = parseFloatAttr(args->Attribute("intensity"));
        out.maxDistance = parseFloatAttr(args->Attribute("maxDistance"));
    }
}

void parseSpotLight(tinyxml2::XMLElement* el, ParsedLight& out)
{
    out.type = LightType::Spot;
    out.position = parseVec3Child(el, "Position");
    out.direction = parseVec3Child(el, "Direction");
    out.color = parseColorChild(el, "Color");
    auto* args = el->FirstChildElement("Args");
    if (args) {
        out.innerAngle = parseFloatAttr(args->Attribute("innerAngle"));
        out.outerAngle = parseFloatAttr(args->Attribute("outerAngle"));
        out.intensity = parseFloatAttr(args->Attribute("intensity"));
        out.maxDistance = parseFloatAttr(args->Attribute("maxDistance"));
    }
}

void parseDirectionalLight(tinyxml2::XMLElement* el, ParsedLight& out)
{
    out.type = LightType::Directional;
    out.direction = parseVec3Child(el, "Direction");
    out.color = parseColorChild(el, "Color");
    auto* intensityEl = el->FirstChildElement("Intensity");
    if (intensityEl)
        out.intensity = parseFloatAttr(intensityEl->Attribute("value"));
}

} // anonymous namespace

void convertToPBR(ParsedModel& m)
{
    m.metallic = (m.albedo.z >= 0.5f) ? 1.0f : 0.0f;
    m.roughness = std::sqrt(2.0f / (m.shininess + 2.0f));
    m.transparency = glm::clamp(m.albedo.w, 0.0f, 1.0f);
}

SceneConfig loadSceneConfig(const std::string& xmlPath, const std::string& modelBasePath)
{
    SceneConfig cfg;

    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(xmlPath.c_str()) != tinyxml2::XML_SUCCESS)
        return cfg;

    auto* root = doc.FirstChildElement("SceneInfo");
    if (!root) return cfg;

    // ── DepthMax ──
    if (auto* dm = root->FirstChildElement("DepthMax"))
        if (auto* args = dm->FirstChildElement("Args"))
            cfg.maxDepth = args->IntAttribute("DepthMax", 4);

    // ── Camera ──
    if (auto* cam = root->FirstChildElement("Camera")) {
        if (auto* args = cam->FirstChildElement("Args")) {
            cfg.width = args->IntAttribute("width", 256);
            cfg.height = args->IntAttribute("height", 192);
            const char* outName = args->Attribute("outputname");
            if (outName) cfg.outputName = outName;
        }
    }

    // ── EnvironmentMap ──
    if (auto* env = root->FirstChildElement("EnvironmentMap"))
        if (auto* args = env->FirstChildElement("Args"))
            cfg.envMapDisplay = args->IntAttribute("display", 0) != 0;

    // ── Models (all known model tag names) ──
    for (auto* el = root->FirstChildElement(); el; el = el->NextSiblingElement()) {
        const char* tag = el->Name();
        if (!tag) continue;

        if (kModelTagNames.count(tag)) {
            ParsedModel m;
            parseModelElement(el, m);
            if (!m.filename.empty())
                m.filename = modelBasePath + m.filename;
            cfg.models.push_back(std::move(m));
        }
    }

    // ── Lights ──
    if (auto* pl = root->FirstChildElement("PointLight2")) {
        ParsedLight l;
        parsePointLight(pl, l);
        cfg.lights.push_back(l);
    }
    if (auto* sl = root->FirstChildElement("SpotLight")) {
        ParsedLight l;
        parseSpotLight(sl, l);
        cfg.lights.push_back(l);
    }
    if (auto* dl = root->FirstChildElement("DirectionalLight")) {
        ParsedLight l;
        parseDirectionalLight(dl, l);
        cfg.lights.push_back(l);
    }

    // ── Ambient ──
    if (auto* amb = root->FirstChildElement("AmbientAgrs")) {
        if (auto* s = amb->FirstChildElement("Strength"))
            cfg.ambientStrength = parseFloatAttr(s->Attribute("value"));
        cfg.ambientColor = parseColorChild(amb, "Color");
    }

    // ── Diffuse/specular strength scalars ──
    if (auto* str = root->FirstChildElement("Strength")) {
        if (auto* ds = str->FirstChildElement("diffuseStrength"))
            cfg.diffuseStrength = parseFloatAttr(ds->Attribute("value"));
        if (auto* ss = str->FirstChildElement("specularStrength"))
            cfg.specularStrength = parseFloatAttr(ss->Attribute("value"));
    }

    // ── Default lights if none parsed ──
    if (cfg.lights.empty()) {
        cfg.lights.push_back({LightType::Point, {-20, 20, 20}, {}, {1,1,1}, 1.5f, 30.0f});
        cfg.lights.push_back({LightType::Point, {30, 50, -25}, {}, {1,1,1}, 1.8f, 30.0f});
        cfg.lights.push_back({LightType::Point, {30, 20, 30}, {}, {1,1,1}, 1.7f, 30.0f});
    }

    return cfg;
}

} // namespace RYRayTracing
