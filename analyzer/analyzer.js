/*
 * piranha-analyzer
 */

var Base64 = {
    CSET: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",

    // FIXME: This leaves a little junk at the end for the padding. This
    // doesn't matter for EBML, but if you reuse this routine, beware.
    decode: function(str) {
        // Create the lookup table: the "reverse character set".
        if (!this._rCSet) {
            this.rCSet = {};
            for (var i = 0; i < this.CSET.length; i++)
                this.rCSet[this.CSET.charAt(i)] = i;
        }

        var origin = str.indexOf(',') + 1;  // useful if it's a data: URL
        var size = str.length - origin * 3/4;
        var buf = new ArrayBuffer(size);

        var arr = new Uint8Array(buf);
        for (var srcI = 0, destI = 0; srcI < size; srcI += 4, destI += 3) {
            var a = this.rCSet[str.charAt(origin+srcI)];
            var b = this.rCSet[str.charAt(origin+srcI+1)];
            var c = this.rCSet[str.charAt(origin+srcI+2)];
            var d = this.rCSet[str.charAt(origin+srcI+3)];
            arr[destI]   = (a << 2) | (b >> 4);
            arr[destI+1] = (b << 4) | (c >> 2);
            arr[destI+2] = (c << 6) | d;
        }

        return buf;
    }
};

// A simple EBML reader on the cursor model.

function EBMLReader(buffer) {
    this._array = new Uint8Array(buffer);
    this.reset();
}

EBMLReader.prototype = {
    get isLastSibling() {
        if (!this._stack.length)
            return false;   // FIXME: broken on the top level - use file size

        var parent = this._stack[this._stack.length - 1];
        return this._pos + this.size >= parent.pos + parent.size;
    },

    forEachChild: function(callback, target) {
        this.moveToFirstChild();
        while (true) {
            callback.call(target);
            if (this.isLastSibling)
                break;
            this.moveToNextSibling();
        }
        this.moveToParent();
    },

    moveToFirstChild: function() {
        this._stack.push({ pos: this._pos, size: this.size, tag: this.tag });
        this._readTag();
    },

    moveToNextSibling: function() {
        this._pos += this.size;
        this._readTag();
    },

    moveToParent: function() {
        if (!this._stack.length)
            throw new Error("EBMLReader.moveToParent: At the top level!");

        var state = this._stack.pop();
        this._pos = state.pos;
        this.size = state.size;
        this.tag = state.tag;
    },

    readCString: function(offset) {
        var pos = this._pos + offset;
        var chars = [];
        var ch;
        while ((ch = this._array[pos++]))
            chars.push(String.fromCharCode(ch));
        return chars.join("");
    },

    readUInt32: function(offset) {
        var pos = this._pos + offset;
        return ((this._array[pos] << 24) | (this._array[pos+1] << 16) |
            (this._array[pos+2] << 8) | this._array[pos+3]) >>> 0;
    },

    reset: function() {
        this._pos = 0;
        this._stack = [];
        this._tag = this._readTag();
    },

    _readRawVInt: function() {
        var a = this._array[this._pos++];
        if (a & 0x80)
            return a;
        var b = this._array[this._pos++];
        if (a & 0x40)
            return (a << 8) | b;
        var c = this._array[this._pos++];
        if (a & 0x20)
            return (a << 16) | (b << 8) | c;
        var d = this._array[this._pos++];
        if (a & 0x10)
            return (a << 24) | (b << 16) | (c << 8) | d;
        throw new Error("Invalid EBML vint!");
    },

    _readTag: function() {
        var pos = this._pos;
        this.tag = this._readRawVInt();
        this.size = this._readVInt();
    },

    _readVInt: function() {
        var a = this._array[this._pos++];
        if (a & 0x80)
            return a & 0x7f;
        var b = this._array[this._pos++];
        if (a & 0x40)
            return ((a & 0x3f) << 8) | b;
        var c = this._array[this._pos++];
        if (a & 0x20)
            return ((a & 0x1f) << 16) | (b << 8) | c;
        var d = this._array[this._pos++];
        if (a & 0x10)
            return ((a & 0x0f) << 24) | (b << 16) | (c << 8) | d;
        throw new Error("Invalid EBML vint!");
    }
};

// The data model

function Model(buffer) {
    this._buffer = buffer;
    this._reader = new EBMLReader(buffer);
    this._maps = this._loadMemoryMap();
    this._symbols = this._loadSymbols();

    var samples = this._loadSamples();
    this.threads = samples.threads;
    this.totalSamples = samples.totalSamples;
}

Model.prototype = {
    EBML_MEMORY_MAP_TAG: 0x81,
    EBML_MEMORY_REGION_TAG: 0x82,
    EBML_SAMPLES_TAG: 0x83,
    EBML_SAMPLE_TAG: 0x84,
    EBML_THREAD_SAMPLE_TAG: 0x85,
    EBML_THREAD_STATUS_TAG: 0x86,
    EBML_STACK_TAG: 0x87,
    EBML_SYMBOLS_TAG: 0x88,
    EBML_MODULE_TAG: 0x89,
    EBML_MODULE_NAME_TAG: 0x8a,
    EBML_SYMBOL_TAG: 0x8b,
    EBML_THREAD_PID_TAG: 0x8c,

    _loadMemoryMap: function() {
        this._reader.reset();
        while (this._reader.tag !== this.EBML_MEMORY_MAP_TAG)
            this._reader.moveToNextSibling();
        this._reader.moveToFirstChild();

        var maps = {};
        while (!this._reader.isLastSibling) {
            var name = this._reader.readCString(12);
            maps[name] = {
                start: this._reader.readUInt32(0),
                end: this._reader.readUInt32(4),
                offset: this._reader.readUInt32(8)
            };

            console.log("map:" + name + ":" + maps[name].start.toString(16) +
                "-" + maps[name].end.toString(16) +
                " @ " + maps[name].offset.toString(16));

            this._reader.moveToNextSibling();
        }

        return maps;
    },

    _loadSamples: function() {
        this._reader.reset();
        while (this._reader.tag !== this.EBML_SAMPLES_TAG)
            this._reader.moveToNextSibling();

        var threads = {}, totalSamples = 0;
        this._reader.forEachChild(function() {
            if (this._reader.tag != this.EBML_SAMPLE_TAG)
                throw new Error("_loadSamples: non-sample in sample list");

            this._reader.forEachChild(function() {
                if (this._reader.tag != this.EBML_THREAD_SAMPLE_TAG) {
                    throw new Error("_loadSamples: non-thread sample in " +
                        "sample list");
                }

                var threadPID, threadRunning, stack;
                this._reader.forEachChild(function() {
                    switch (this._reader.tag) {
                    case this.EBML_THREAD_PID_TAG:
                        threadPID = this._reader.readUInt32(0);
                        break;
                    case this.EBML_THREAD_STATUS_TAG:
                        threadRunning = this._reader.readCString() != "S";
                        break;
                    case this.EBML_STACK_TAG:
                        stack = [];
                        //console.log("pos=", this._reader._pos);
                        /*if (this._reader.size > 8)
                            throw new Error("size is " + this._reader.size);*/
                        for (var i = 0; i < this._reader.size; i += 4) {
                            var addr = this._reader.readUInt32(i);
                            //console.log("addr=", addr.toString(16));
                            stack.push(this._symbolicateAddress(addr));
                        }
                        break;
                    }
                }, this);

                if (!(threadPID in threads)) {
                    threads[threadPID] = {
                        bottomUp: { c: {} },
                        topDown: { c: {} }
                    };
                }

                // Add the data to the appropriate bottom-up call stack.
                /*if (stack.length > 2)
                    throw new Error("stack length " + stack.length);*/

                var node = threads[threadPID].bottomUp;
                for (var i = 0; i < stack.length; i++) {
                    var symbol = stack[i];
                    if (!(symbol in node.c))
                        node.c[symbol] = { n: 0, c: {} };
                    node = node.c[symbol];
                    node.n++;
                }

                // And to the appropriate top-down call stack.
                node = threads[threadPID].topDown;
                for (var i = stack.length - 1; i >= 0; i--) {
                    var symbol = stack[i];
                    if (!(symbol in node.c))
                        node.c[symbol] = { n: 0, c: {} };
                    node = node.c[symbol];
                    node.n++;
                }
            }, this);

            totalSamples++;
        }, this);

        console.log("Read " + totalSamples + " samples\n");

        return { threads: threads, totalSamples: totalSamples };
    },

    _loadSymbols: function() {
        this._reader.reset();
        while (this._reader.tag !== this.EBML_SYMBOLS_TAG) {
            console.log("tag:" + this._reader.tag.toString(16));
            this._reader.moveToNextSibling();
        }

        // Find all the modules.
        var symbols = [];
        this._reader.forEachChild(function() {
            if (this._reader.tag != this.EBML_MODULE_TAG)
                throw new Error("_loadSymbols: non-module in module list");

            var moduleSymbols = [], moduleName = null;
            this._reader.forEachChild(function() {
                if (this._reader.tag == this.EBML_MODULE_NAME_TAG) {
                    moduleName = this._reader.readCString(0);
                } else if (this._reader.tag == this.EBML_SYMBOL_TAG) {
                    var symbol = {
                        addr: this._reader.readUInt32(0),
                        name: this._reader.readCString(4)
                    };
                    moduleSymbols.push(symbol);
                } else {
                    console.warn("_loadSymbols: non-module name or symbol " +
                        "in module");
                }
            }, this);

            if (moduleName == null)
                throw new Error("Unnamed module found!");

            var module = this._maps[moduleName];
            if (!module) {
                throw new Error("No module info found for '" + moduleName +
                    "'");
            }

            moduleSymbols.forEach(function(symbol) {
                symbols.push({
                    module: moduleName,
                    name: symbol.name,
                    addr: module.start - module.offset + symbol.addr
                });
            }, this);
        }, this);

        symbols.sort(function(a, b) { return a.addr - b.addr; });

        console.log("sym:", symbols[0], symbols[0].addr.toString(16),
            symbols[symbols.length-1],
            symbols[symbols.length-1].addr.toString(16),
            symbols.length);

        /*document.write(symbols.map(function(s) {
                return "<div>" + s.name + " : " + s.module + " @ " +
                    s.addr.toString(16) + "</div>";
            }).join(""));*/

        return symbols;
    },

    _symbolicateAddress: function(addr) {
        // Binary search to find the right symbol.
        var lo = 0, hi = this._symbols.length;
        while (lo < hi) {
            var mid = ((lo + hi) / 2) | 0;
            var loAddr = this._symbols[mid].addr;
            if (addr < loAddr) {
                hi = mid;
                continue;
            }

            var hiAddr = (mid == this._symbols.length - 1) ?
                this._maps[this._symbols[mid].module].end :
                this._symbols[mid+1].addr;
            if (addr >= hiAddr) {
                lo = mid + 1;
                continue;
            }

            var symbol = this._symbols[mid];
            //console.log("symbol=", symbol);
            // TODO: include module name as well
            return symbol.name;
        }

        // TODO: fall back to the module name
        return addr.toString(16);
    }
};

// The controller

function Controller() {
    var self = this;
    $('#uploader').change(function() {
        var files = this.files;
        for (var i = 0; i < files.length; i++) {
            var reader = new FileReader();
            reader.onload = self._fileLoaded.bind(self);
            reader.readAsDataURL(files[i]);
        }
    });

    $('#threads').change(this._showCurrentThread.bind(this));
}

Controller.prototype = {
    _fileLoaded: function(ev) {
        this._model = new Model(Base64.decode(ev.target.result));
        this._populateThreadSelector();
    },

    _populateThreadSelector: function() {
        var options = [];
        Object.getOwnPropertyNames(this._model.threads).forEach(function(id) {
            options.push('<option value=' + id + '>' + id + '</option>');
        });
        $('#threads').html(options.join(''));
        $('#threads > option[0]').attr('selected', 'selected');

        this._showCurrentThread();
    },

    _showCurrentThread: function() {
        var id = $('#threads > option:selected').attr('value');
        console.log("*** id=" + id);
        this._showTreeForThread(id, $('#bottom-up'), 'bottomUp');
        this._showTreeForThread(id, $('#top-down'), 'topDown');
    },

    _showTreeForThread: function(threadID, $element, property) {
        var totalSamples = this._model.totalSamples;

        var html = [];
        function process(node) {
            html.push('<ul>');
            var syms = Object.getOwnPropertyNames(node.c);
            syms.sort(function(a, b) { return node.c[b].n - node.c[a].n });
            syms.forEach(function(name) {
                var percent = node.c[name].n / totalSamples * 100;
                html.push('<li>(', percent.toPrecision(2), '%) ', name);
                process(node.c[name]);
                html.push('</li>');
            });
            html.push('</ul>');
        }

        process(this._model.threads[threadID][property]);
        $element.html(html.join(''));
    }
}

$(function() { new Controller });
