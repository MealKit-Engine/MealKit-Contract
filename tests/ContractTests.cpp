// mkcontract tests (no network, no window): writes a fake user folder, tools and live info files under the build folder and reads them back.
//   mkcontract_tests   PASS or FAILED at the end
#include "mkcontract/Client.h"
#include "mkcontract/Files.h"
#include "mkcontract/Json.h"
#include "mkcontract/Live.h"
#include "mkcontract/Modules.h"
#include "mkcontract/Text.h"
#include "mkcontract/Tool.h"
#include "mkcontract/User.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace mkcontract;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::printf("  FAIL %s (%s:%d)\n", #cond, __FILE__, __LINE__);                                             \
            ++g_failures;                                                                                              \
        }                                                                                                              \
    } while (0)

std::string str(const fs::path& p) { return utf8Of(p); }

void setUserDirectory(const fs::path& dir) {
#if defined(_WIN32)
    _putenv_s("MEALKIT_USER_DIR", str(dir).c_str());
#else
    setenv("MEALKIT_USER_DIR", str(dir).c_str(), 1);
#endif
}

void testText() {
    const std::vector<std::string> lines = splitLines("\xEF\xBB\xBF# comment\r\n\n  @format tool 2 \r\nname  Audio\n");
    CHECK(lines.size() == 3 && lines[0] == "# comment" && lines[1] == "@format tool 2" && lines[2] == "name  Audio");

    FormatLine f;
    CHECK(parseFormatLine("name x", f) == FormatParse::NotFormat);
    CHECK(parseFormatLine("@format scene 4", f) == FormatParse::Ok && f.kind == "scene" && f.version == 4);
    CHECK(parseFormatLine("@format scene", f) == FormatParse::Malformed);
    CHECK(parseFormatLine("@format scene 0", f) == FormatParse::Malformed);
    CHECK(parseFormatLine("@format scene 2x", f) == FormatParse::Malformed);
    CHECK(parseFormatLine("@version 2", f) == FormatParse::Malformed);
    CHECK(formatLine("table", 1) == "@format table 1");

    std::string k, v;
    CHECK(splitKeyValue("name  My Game ", k, v) && k == "name" && v == "My Game");
    CHECK(splitKeyValue("language=ko", k, v) && k == "language" && v == "ko");
    CHECK(splitKeyValue("flag", k, v) && k == "flag" && v.empty());
    CHECK(!splitKeyValue("# name x", k, v) && !splitKeyValue("@format x 1", k, v) && !splitKeyValue("  ", k, v));
    CHECK(splitWords(" .table\t.wav ") == (std::vector<std::string>{".table", ".wav"}));
}

void testFiles(const fs::path& root) {
    const std::string list = str(root / "lists" / "a.txt");
    CHECK(writeList(list, {"one", "two"}));
    CHECK(readList(list) == (std::vector<std::string>{"one", "two"}));
    CHECK(readList(str(root / "missing.txt")).empty());

    const std::string keys = str(root / "keys.txt");
    CHECK(writeFile(keys, "# c\n@format settings 1\nlanguage ko\ntoolPath A\ntoolPath B\n"));
    CHECK(readKey(keys, "language") == "ko");
    CHECK(readKey(keys, "missing").empty());
    CHECK(readKeys(keys, "toolPath") == (std::vector<std::string>{"A", "B"}));

    const std::string dir = str(root / "lists");
    CHECK(sameDirectory(dir, dir + "/"));
    CHECK(sameDirectory(dir, str(root / "lists" / ".." / "lists")));
    CHECK(!sameDirectory(dir, str(root)));
#if defined(_WIN32)
    std::string upper = dir;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    CHECK(sameDirectory(dir, upper));
#endif
    std::vector<std::string> l = {dir, str(root)};
    CHECK(containsDirectory(l, dir + "/"));
    eraseDirectory(l, dir);
    CHECK(l.size() == 1);
}

void testUser(const fs::path& root) {
    const fs::path user = root / "user";
    setUserDirectory(user);
    CHECK(sameDirectory(userDirectory(), str(user)));
    const std::string a = str(root / "games" / "A"), b = str(root / "games" / "B"), c = str(root / "games" / "C");
    addRecentProject(a);
    addRecentProject(b);
    addRecentProject(a + "/"); // same folder: moves to the top
    std::vector<std::string> recent = recentProjects();
    CHECK(recent.size() == 2 && sameDirectory(recent[0], a) && sameDirectory(recent[1], b));
    setFavoriteProject(b, true);
    CHECK(favoriteProjects().size() == 1);
    replaceProjectDirectory(b, c);
    recent = recentProjects();
    CHECK(sameDirectory(recent[1], c) && sameDirectory(favoriteProjects()[0], c));
    removeRecentProject(c);
    CHECK(recentProjects().size() == 1 && favoriteProjects().empty());
    for (int i = 0; i < 120; ++i) addRecentProject(str(root / "many" / std::to_string(i)));
    CHECK(recentProjects().size() == kMaxRecent);

    addEngine(a);
    addEngine(a + "/");
    CHECK(addedEngines().size() == 1);

    writeFile(userPath(kEditorSettings), "# editor\nlanguage en\ntoolPath " + str(root / "extra") + "\n");
    CHECK(editorSetting("language") == "en");
    CHECK(toolPaths().size() == 1);
    CHECK(setEditorSetting("language", "ko") && editorSetting("language") == "ko" && toolPaths().size() == 1);
    std::string settings;
    readFile(userPath(kEditorSettings), settings);
    CHECK(settings.rfind("# editor\nlanguage ko\n", 0) == 0); // the comment and the line's place are kept
    CHECK(setEditorSetting("language", "") && editorSetting("language").empty() && toolPaths().size() == 1);
}

void testTools(const fs::path& root) {
    const fs::path installed = root / "user" / kToolsFolder;
    writeFile(str(installed / "audio" / kToolManifest),
              "# MealKit tool manifest\n@format tool 1\nname Audio\nexe bin/MealKitAudio.exe\nversion 0.2.0\nprotocol 1\nopens .events .wav\n"
              "summary Game sound events\nicon audio\n");
    writeFile(str(installed / "broken" / kToolManifest), "name Broken\nexe x.exe\n"); // no protocol
    writeFile(str(installed / "newer" / kToolManifest), "@format tool 2\nname Newer\nexe n.exe\nprotocol 1\nshape round\n");
    // A toolPath folder that is itself a tool, with a name that's already taken: the first one (installed) wins
    writeFile(str(root / "extra" / kToolManifest), "@format tool 1\nname Audio\nexe other.exe\nprotocol 1\n");

    ToolManifest t;
    std::string error;
    CHECK(readToolManifest(str(installed / "audio"), t, &error));
    CHECK(t.name == "Audio" && t.version == "0.2.0" && t.protocol == 1 && t.warnings.empty());
    CHECK(t.opens == (std::vector<std::string>{".events", ".wav"}));
    CHECK(t.summary == "Game sound events" && t.icon == "audio");
    CHECK(pathOf(t.exe).is_absolute() && pathOf(t.exe).filename() == "MealKitAudio.exe");
    CHECK(!readToolManifest(str(installed / "broken"), t, &error) && !error.empty());
    CHECK(!readToolManifest(str(root / "nothing"), t, &error));
    CHECK(readToolManifest(str(installed / "newer"), t) && t.warnings.size() == 2); // newer format + unknown key

    const std::vector<std::string> roots = toolSearchRoots();
    CHECK(roots.size() == 2);
    std::vector<std::string> errors;
    const std::vector<ToolManifest> tools = findTools(roots, &errors);
    CHECK(tools.size() == 2 && tools[0].name == "Audio" && tools[1].name == "Newer");
    CHECK(tools.size() == 2 && pathOf(tools[0].exe).filename() == "MealKitAudio.exe");
    CHECK(errors.size() == 1);

    CHECK(writeAvailableTools({{"MkRender", "0.2.0", "Scenes\tand materials", "render"}, {"MkUI", "0.1.5", "", ""}}));
    const std::vector<AvailableTool> available = readAvailableTools();
    CHECK(available.size() == 2 && available[0].name == "MkRender" && available[0].summary == "Scenes and materials" && available[0].icon == "render");
    CHECK(available.size() == 2 && available[1].version == "0.1.5" && available[1].summary.empty());
    // A command line folder comes first and overrides
    CHECK(findTools(toolSearchRoots({str(root / "extra")}))[0].exe.find("other.exe") != std::string::npos);
}

void testJson() {
    Json j;
    const char* text = R"( {"a":1.5,"b":[true,false,null],"c":{"d":"x\"y\\z\n"},"e":"é😀"} )";
    CHECK(Json::parse(text, j));
    CHECK(j.type == Json::Type::Object && j["a"].num() == 1.5);
    CHECK(j["b"].array.size() == 3 && j["b"].array[0].flag() && !j["b"].array[1].flag(true) && j["b"].array[2].type == Json::Type::Null);
    CHECK(j["c"]["d"].str() == "x\"y\\z\n");
    CHECK(j["e"].str() == "\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(j["missing"]["deeper"].str("none") == "none");
    CHECK(!Json::parse("{\"a\":1} extra", j));
    CHECK(!Json::parse("{\"a\":", j));
    CHECK(!Json::parse("", j));
    std::string deep(40, '[');
    CHECK(!Json::parse(deep + std::string(40, ']'), j));

    const std::string written = jsonString("q\"\\\n\t\x01");
    CHECK(written == "\"q\\\"\\\\\\n\\t\\u0001\"");
    CHECK(Json::parse(written, j) && j.str() == "q\"\\\n\t\x01");
    CHECK(jsonList({"a", "b"}) == "[\"a\",\"b\"]" && jsonList({}) == "[]");
}

void testLive(const fs::path& root) {
    const fs::path project = root / "project";
    live::Instance in;
    in.protocol = live::kProtocol;
    in.kind = "editor";
    in.pid = 1234;
    in.port = 27220;
    in.token = "abc";
    in.project = "Game \"1\"";
    in.engine = "0.7.6";
    in.url = "http://127.0.0.1:27220/abc/";
    CHECK(live::infoFileName("editor", 1234) == "editor-1234.json");
    CHECK(live::infoFilePid("game-77.json") == 77 && live::infoFilePid("x.json") == 0);
    writeFile(str(project / live::kInfoFolder / live::infoFileName(in.kind, in.pid)), live::infoJson(in));
    writeFile(str(project / live::kInfoFolder / "game-5.json"), "{\"kind\":\"game\"}"); // no port/token: skipped
    writeFile(str(project / live::kInfoFolder / "notes.txt"), "{}");
    const std::vector<live::Instance> found = live::findInstances(str(project));
    CHECK(found.size() == 1);
    if (found.size() == 1) {
        const live::Instance& f = found[0];
        CHECK(f.kind == "editor" && f.pid == 1234 && f.port == 27220 && f.token == "abc" && f.project == in.project &&
              f.engine == "0.7.6" && f.url == in.url && f.protocol == live::kProtocol);
    }
    CHECK(live::findInstances(str(root / "nowhere")).empty());

    CHECK(live::urlEncode("a b/é~") == "a%20b%2F%C3%A9~");
    live::Instance closed;
    closed.port = 1; // nothing listens on port 1
    closed.token = "x";
    CHECK(live::request(closed, "GET", "api/hello", {}, 100).status == 0);
}

void testModules() {
    const ModuleList list = parseModuleList("installed\tcooldown\t0.1.0\t2\t1\thttps://example.com/c.git\r\n"
                                            "module\thealth\t-\t0.3.0\t0.8\tHealth\tDamage\tcombat\tme\thttps://example.com/h\n"
                                            "something new\n"
                                            "error\toffline");
    CHECK(list.installed.size() == 1 && list.installed[0].name == "cooldown" && list.installed[0].modified == 2 &&
          list.installed[0].pending == 1 && list.installed[0].source == "https://example.com/c.git");
    CHECK(list.registry.size() == 1 && list.registry[0].version.empty() && list.registry[0].latest == "0.3.0" &&
          list.registry[0].latestEngine == "0.8" && list.registry[0].repo == "https://example.com/h");
    CHECK(list.error == "offline");
    CHECK(list.installed[0].type.empty() && list.registry[0].icon.empty()); // older engine: no later columns

    const ModuleList full = parseModuleList("installed\taudio\t0.1.2\t0\t0\tengine audio\tengine\taudio\tSound\tengine audio\taudio\n"
                                            "module\tcooldown\t0.1.0\t0.1.0\t0.5\tCooldowns\tTimers\tgameplay timer\tme\thttps://example.com/c\ttimer\n");
    CHECK(full.installed.size() == 1 && full.installed[0].type == "engine" && full.installed[0].summary == "Sound" &&
          full.installed[0].tags == "engine audio" && full.installed[0].icon == "audio");
    CHECK(full.registry.size() == 1 && full.registry[0].icon == "timer");
    CHECK(moduleIconKeyword("timer", "gameplay", "x") == "timer" && moduleIconKeyword("nope", "engine ui", "x") == "ui" &&
          moduleIconKeyword("", "engine", "online-steam") == "online" && moduleIconKeyword("", "", "zzz").empty());
    CHECK(hasTag("gameplay timer ui", "timer") && hasTag("ui", "ui") && !hasTag("gameplay timers", "timer") && !hasTag("", "ui"));
}

} // namespace

int main() {
    const fs::path root = fs::current_path() / "contract_test_files";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);

    testText();
    testFiles(root);
    testUser(root);
    testTools(root);
    testJson();
    testLive(root);
    testModules();

    fs::remove_all(root, ec);
    std::printf(g_failures ? "FAILED (%d)\n" : "PASS\n", g_failures);
    return g_failures ? 1 : 0;
}
