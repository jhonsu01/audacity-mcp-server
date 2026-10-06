// "MCP Audio Tools: status" - checks that the native library and Audacity's codecs load.
function main() {
    const Native = require("MuseApi.Native");
    const lib = Native.open("audacity_mcp_native");
    if (!lib) {
        api.interactive.error("MCP Audio Tools", "The native library (audacity_mcp_native.dll) could not be loaded.");
        return false;
    }
    const v = JSON.parse(lib.dispatch("version", []));
    api.interactive.info("MCP Audio Tools",
        "Native library OK.\n" +
        "Codec engine: " + (v.sndfile || ("not available: " + v.sndfileError)) + "\n" +
        "MP3 decoding: " + (v.mp3 ? "yes" : "no") + "\n\n" +
        "Effects added: Tools > Split into Files (MCP), Export Selection to File (MCP), " +
        "Import Audio File as Track (MCP); Analyze > Labels Every N Seconds (MCP).");
    return true;
}
