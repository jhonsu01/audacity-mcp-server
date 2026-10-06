// MCP Audio Tools - Audacity 4 extension effects.
// The script engine cannot touch files, so file I/O goes through the bundled native library
// (platform/windows/x86_64/audacity_mcp_native.dll), which uses Audacity's own libsndfile/mpg123.

const Project = require("Audacity.Project");
const Native = require("MuseApi.Native");

const CHUNK = 65536;

const FORMATS = [
    { token: "wav16", name: "WAV 16-bit", format: "wav", sample: "pcm16", ext: "wav" },
    { token: "wav24", name: "WAV 24-bit", format: "wav", sample: "pcm24", ext: "wav" },
    { token: "wav32f", name: "WAV 32-bit float", format: "wav", sample: "float32", ext: "wav" },
    { token: "flac16", name: "FLAC 16-bit", format: "flac", sample: "pcm16", ext: "flac" },
    { token: "flac24", name: "FLAC 24-bit", format: "flac", sample: "pcm24", ext: "flac" },
    { token: "ogg", name: "OGG Vorbis", format: "ogg", sample: "vorbis", ext: "ogg" },
    { token: "opus", name: "Opus (48 kHz projects)", format: "opus", sample: "opus", ext: "opus" },
    { token: "aiff16", name: "AIFF 16-bit", format: "aiff", sample: "pcm16", ext: "aiff" },
];

function formatChoices() {
    return FORMATS.map(function (f) { return { token: f.token, name: f.name }; });
}

function formatOf(token) {
    for (let i = 0; i < FORMATS.length; ++i) {
        if (FORMATS[i].token === token) {
            return FORMATS[i];
        }
    }
    return FORMATS[0];
}

function openLibrary() {
    const lib = Native.open("audacity_mcp_native");
    if (!lib) {
        throw new Error("The MCP Audio Tools native library could not be loaded. Reinstall the extension.");
    }
    return lib;
}

function call(lib, name, args) {
    return lib.dispatch(name, args);
}

function joinPath(dir, name) {
    const d = String(dir).replace(/[\\/]+$/, "");
    return d + "\\" + name;
}

function safeName(text, fallback) {
    const s = String(text || "").replace(/[\\/:*?"<>|]+/g, "_").replace(/^\s+|\s+$/g, "");
    return s.length > 0 ? s : fallback;
}

function pad(n, width) {
    let s = String(n);
    while (s.length < width) {
        s = "0" + s;
    }
    return s;
}

function extensionOf(path) {
    const m = /\.([A-Za-z0-9]+)$/.exec(String(path));
    return m ? m[1].toLowerCase() : "";
}

// Time range to process: the time selection, or the whole track when nothing is selected in time.
function rangeFor(selection, track) {
    if (selection.duration > 0) {
        return { start: selection.start, end: selection.end };
    }
    return { start: track.start, end: track.end };
}

function selectedTracks(edit) {
    const tracks = edit.selection.audioTracks;
    if (!tracks || tracks.length === 0) {
        throw new Error("Select one or more audio tracks first.");
    }
    return tracks;
}

// Streams [start, end) of a track into consecutive files. Returns the written paths.
function writeTrack(lib, track, start, end, segmentSeconds, makePath, fmt, task, progress) {
    const rate = track.sampleRate;
    const channels = track.channelCount;
    const total = Math.max(0, Math.round((end - start) * rate));
    const segFrames = segmentSeconds > 0 ? Math.max(1, Math.round(segmentSeconds * rate)) : total;
    const reader = track.openReader({ channelCount: channels, sampleRate: rate, start: start, end: end });
    const files = [];
    let done = 0;
    let index = 0;
    while (done < total) {
        const path = makePath(index);
        const writer = call(lib, "writer_open", [path, fmt.format, fmt.sample, channels, rate, 0.6]);
        let inSegment = 0;
        const segLen = Math.min(segFrames, total - done);
        try {
            while (inSegment < segLen) {
                const chunk = reader.read(Math.min(CHUNK, segLen - inSegment));
                if (!chunk) {
                    break;
                }
                const bufs = chunk.channels;
                call(lib, "writer_write", channels > 1 ? [writer, bufs[0], bufs[1]] : [writer, bufs[0]]);
                inSegment += chunk.sampleCount;
                chunk.release();
                if (task && !task.report(Math.min(1, progress.base + progress.span * (done + inSegment) / Math.max(1, total)),
                                         "Writing " + path)) {
                    throw new Error("Cancelled");
                }
            }
        } finally {
            call(lib, "writer_close", [writer]);
        }
        if (inSegment === 0) {
            break;
        }
        files.push(path);
        done += inSegment;
        index += 1;
        if (inSegment < segLen) {
            break;
        }
    }
    return files;
}

// ------------------------------------------------------------------ Split into files
exports.createSplit = function () {
    return {
        parameters: function () {
            return [
                { id: "seconds", name: "Segment length", unit: "s", type: "double", defaultValue: 15.0, min: 0.1, max: 86400.0, step: 0.5,
                  description: "Length of each file in seconds. The last file can be shorter." },
                { id: "folder", name: "Output folder", type: "directory", defaultValue: "",
                  description: "Folder where the files are written." },
                { id: "format", name: "Format", type: "enum", defaultValue: "wav16", choices: formatChoices() },
                { id: "prefix", name: "File name prefix", type: "string", defaultValue: "",
                  description: "Empty: use the track name." },
                { id: "subfolder", name: "One subfolder per track", type: "bool", defaultValue: true },
                { id: "overwrite", name: "Replace existing files", type: "bool", defaultValue: false },
            ];
        },
        validate: function (settings) {
            if (!settings.folder) {
                return "Choose an output folder.";
            }
            return "";
        },
        process: function (settings, session, task) {
            const lib = openLibrary();
            const edit = Project.beginEdit("Split into files", session);
            const tracks = selectedTracks(edit);
            const fmt = formatOf(settings.format);
            const seconds = Number(settings.seconds);
            let written = 0;
            for (let t = 0; t < tracks.length; ++t) {
                const track = tracks[t];
                const range = rangeFor(edit.selection, track);
                const name = safeName(settings.prefix, safeName(track.name, "track" + (t + 1)));
                const folder = settings.subfolder ? joinPath(settings.folder, safeName(track.name, "track" + (t + 1))) : settings.folder;
                call(lib, "mkdir", [folder]);
                const count = Math.ceil((range.end - range.start) / seconds - 1e-9);
                const width = Math.max(3, String(count).length);
                const prefix = tracks.length > 1 && !settings.subfolder && settings.prefix ? name + "_" + (t + 1) : name;
                const makePath = function (i) {
                    const p = joinPath(folder, prefix + "_" + pad(i + 1, width) + "." + fmt.ext);
                    if (!settings.overwrite && call(lib, "exists", [p])) {
                        throw new Error("File already exists: " + p + " (enable 'Replace existing files')");
                    }
                    return p;
                };
                written += writeTrack(lib, track, range.start, range.end, seconds, makePath, fmt, task,
                                      { base: t / tracks.length, span: 1 / tracks.length }).length;
            }
            edit.commit();
            api.log.info("mcp-tools", "Split into " + written + " files");
            return true;
        },
    };
};

// ------------------------------------------------------------------ Export selection
exports.createExport = function () {
    return {
        parameters: function () {
            return [
                { id: "file", name: "Output file", type: "file", defaultValue: "",
                  description: "Full path of the file to write. Several selected tracks get _1, _2... suffixes." },
                { id: "format", name: "Format", type: "enum", defaultValue: "auto",
                  choices: [{ token: "auto", name: "From the file extension" }].concat(formatChoices()) },
                { id: "overwrite", name: "Replace existing file", type: "bool", defaultValue: false },
            ];
        },
        validate: function (settings) {
            return settings.file ? "" : "Choose the output file.";
        },
        process: function (settings, session, task) {
            const lib = openLibrary();
            const edit = Project.beginEdit("Export selection", session);
            const tracks = selectedTracks(edit);
            let fmt = formatOf(settings.format);
            if (settings.format === "auto") {
                const ext = extensionOf(settings.file);
                const byExt = { wav: "wav16", flac: "flac24", ogg: "ogg", oga: "ogg", opus: "opus", aif: "aiff16", aiff: "aiff16" };
                if (!byExt[ext]) {
                    throw new Error("Unknown extension ." + ext + ": pick a format or use .wav, .flac, .ogg, .opus or .aiff");
                }
                fmt = formatOf(byExt[ext]);
            }
            const base = String(settings.file).replace(/\.[A-Za-z0-9]+$/, "");
            for (let t = 0; t < tracks.length; ++t) {
                const path = tracks.length > 1 ? base + "_" + (t + 1) + "." + fmt.ext
                                               : (extensionOf(settings.file) ? settings.file : base + "." + fmt.ext);
                if (!settings.overwrite && call(lib, "exists", [path])) {
                    throw new Error("File already exists: " + path);
                }
                const range = rangeFor(edit.selection, tracks[t]);
                writeTrack(lib, tracks[t], range.start, range.end, 0, function () { return path; }, fmt, task,
                           { base: t / tracks.length, span: 1 / tracks.length });
            }
            edit.commit();
            return true;
        },
    };
};

// ------------------------------------------------------------------ Interval labels
exports.createLabels = function () {
    return {
        parameters: function () {
            return [
                { id: "seconds", name: "Interval", unit: "s", type: "double", defaultValue: 15.0, min: 0.1, max: 86400.0, step: 0.5 },
                { id: "prefix", name: "Label text prefix", type: "string", defaultValue: "Part " },
            ];
        },
        process: function (settings, session, task) {
            const edit = Project.beginEdit("Labels every N seconds", session);
            const sel = edit.selection;
            let start = sel.start;
            let end = sel.end;
            if (!(sel.duration > 0)) {
                const tracks = sel.audioTracks;
                if (!tracks || tracks.length === 0) {
                    throw new Error("Select a time range or an audio track.");
                }
                start = tracks[0].start;
                end = tracks[0].end;
            }
            const step = Number(settings.seconds);
            const labels = edit.createLabelTrack("Every " + step + " s");
            let i = 0;
            for (let t = start; t < end - 1e-9; t += step) {
                i += 1;
                labels.addLabel(t, Math.min(t + step, end), String(settings.prefix) + i);
            }
            edit.commit();
            return true;
        },
    };
};

// ------------------------------------------------------------------ Import file
exports.createImport = function () {
    return {
        parameters: function () {
            return [
                { id: "file", name: "Audio file", type: "file", defaultValue: "",
                  description: "WAV, FLAC, OGG, Opus, MP3, AIFF, CAF, W64, AU..." },
                { id: "at", name: "Start at", unit: "s", type: "double", defaultValue: 0.0, min: 0.0, max: 864000.0, step: 1.0 },
            ];
        },
        validate: function (settings) {
            return settings.file ? "" : "Choose the audio file.";
        },
        process: function (settings, session, task) {
            const lib = openLibrary();
            const reader = call(lib, "reader_open", [settings.file]);
            try {
                const info = JSON.parse(call(lib, "reader_info", [reader]));
                const channels = Math.min(2, info.channels);
                const edit = Project.beginEdit("Import " + settings.file, session);
                const writer = edit.createAudioWriter({ channelCount: channels, sampleRate: info.sampleRate });
                let done = 0;
                for (;;) {
                    const want = info.frames > 0 ? Math.min(CHUNK, info.frames - done) : CHUNK;
                    if (want <= 0) {
                        break;
                    }
                    const chunk = writer.createChunk(want);
                    const bufs = chunk.channels;
                    const got = call(lib, "reader_read", channels > 1 ? [reader, bufs[0], bufs[1]] : [reader, bufs[0]]);
                    if (got <= 0) {
                        break;
                    }
                    if (got < want) {
                        const tail = writer.createChunk(got);
                        // Re-read is not possible: copy the filled prefix through an ArrayBuffer.
                        for (let c = 0; c < channels; ++c) {
                            tail.channels[c].copyFromArrayBuffer(bufs[c].copyToArrayBuffer().slice(0, got * 4));
                        }
                        writer.write(tail);
                        done += got;
                        break;
                    }
                    writer.write(chunk);
                    done += got;
                    if (task && info.frames > 0 && !task.report(Math.min(1, done / info.frames), "Importing")) {
                        throw new Error("Cancelled");
                    }
                }
                const name = String(settings.file).replace(/^.*[\\/]/, "").replace(/\.[^.]+$/, "");
                writer.addTrack(name, Number(settings.at) || 0);
                edit.commit();
            } finally {
                call(lib, "reader_close", [reader]);
            }
            return true;
        },
    };
};
