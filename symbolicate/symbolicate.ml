(*
 *  piranha/symbolicate.ml
 *
 *  Copyright (c) 2011 Mozilla Foundation
 *  Patrick Walton <pcwalton@mozilla.com>
 *)

type build_info = {
    bi_name: string;
    bi_version: string;
    bi_build_id: string;
}

type memory_region = {
    mr_start: int32;
    mr_end: int32;
    mr_offset: int32;
    mr_name: string;
    mr_path: string;
}

type program_options = {
    po_input_path: string;
    po_output_path: string;
    po_application_ini: string option;
    po_binary_path: string option;
}

type caches = {
    ca_fennec: string;
    ca_syslibs: string;
}

type symbol_sources = {
    ss_cache_dirs: caches;
    ss_symbol_urls: (string, string) Hashtbl.t;
    ss_binary_path: string option;
}

(*
 * General utilities
 *)

let mkdir_p path =
    ignore begin
        List.fold_left begin fun pathname component ->
            let pathname = pathname ^ component ^ "/" in
            begin
                try Unix.mkdir pathname 0o775
                with
                | Unix.Unix_error(Unix.EEXIST, _, _)
                | Unix.Unix_error(Unix.EISDIR, _, _) -> ()
            end;
            pathname
        end "" (ExtString.String.nsplit path "/")
    end

let get_program_options() =
    let oparser =
        OptParse.OptParser.make
            ~usage:"%prog [options] INPUT-PATH OUTPUT-PATH"
            ~version:"0.1"
            () in
    let application_ini = OptParse.Opt.value_option "PATH" None
        Std.identity (fun e s -> s) in
    let binary_path = OptParse.Opt.value_option "PATH" None Std.identity
        (fun e s -> s) in
    OptParse.OptParser.add
        oparser
        ~help:"application.ini file for Fennec"
        ~short_name:'a'
        ~long_name:"application-ini"
        application_ini;
    OptParse.OptParser.add
        oparser
        ~help:"path to local files containing symbol-rich binaries"
        ~short_name:'b'
        ~long_name:"binary-path"
        binary_path;
    let remaining_args = OptParse.OptParser.parse_argv oparser in
    if List.length remaining_args <> 2 then begin
        OptParse.OptParser.usage oparser ();
        exit 1
    end;
    {
        po_input_path = List.hd remaining_args;
        po_output_path = List.nth remaining_args 1;
        po_application_ini = OptParse.Opt.opt application_ini;
        po_binary_path = OptParse.Opt.opt binary_path;
    }

let get_build_info application_ini =
    let f = 
        match application_ini with
        | None ->
            fst (Unix.open_process
                "adb shell run-as org.mozilla.fennec cat application.ini")
        | Some path -> open_in path in
    Std.finally (fun() -> close_in f) begin fun() ->
        let kvps = Hashtbl.create 0 in
        Std.finally (fun() -> close_in f) begin fun() ->
            Enum.iter begin fun line ->
                let line = ExtString.String.strip line in
                if String.contains line '=' then begin
                    let k, v = ExtString.String.split line "=" in
                    Hashtbl.add kvps k v
                end
            end (Std.input_lines f)
        end ();
        {
            bi_name = Hashtbl.find kvps "Name";
            bi_version = Hashtbl.find kvps "Version";
            bi_build_id = Hashtbl.find kvps "BuildID";
        }
    end ()

let get_symbol_urls build_info =
    let app = String.lowercase build_info.bi_name in
    let url = Printf.sprintf
        "http://symbols.mozilla.org/%s/%s-%s-Android-%s-mozilla-central-\
        symbols.txt"
        app
        app
        build_info.bi_version
        build_info.bi_build_id in
    Printf.eprintf "Fetching '%s'..." url;

    let curl = Curl.init() in
    let response = Std.finally (fun() -> Curl.cleanup curl) begin fun() ->
        let buf = Buffer.create 0 in
        Curl.set_writefunction curl begin fun s ->
            Buffer.add_string buf s; String.length s
        end;
        Curl.set_url curl url;
        Curl.perform curl;
		if (Curl.get_responsecode curl) <> 200 then begin
			failwith
				(Printf.sprintf "Couldn't fetch '%s' (%d)" url
					(Curl.get_responsecode curl))
		end;
        Buffer.contents buf
    end () in

    prerr_endline "done.";

    let urls = Hashtbl.create 0 in
    List.iter begin fun line ->
        let line = ExtString.String.strip line in
        if String.contains line '/' then
            let url = Printf.sprintf "http://symbols.mozilla.org/%s/%s" app
                line in
            Hashtbl.add urls (fst (ExtString.String.split line "/")) url
    end (ExtString.String.nsplit response "\n");
    urls

module EBML = struct
    type writer = {
        wr_file: out_channel;
        wr_stack: int Stack.t;
    }

    let tag_memory_map = Int32.of_int 0x81
    let tag_memory_region = Int32.of_int 0x82
    let tag_symbols = Int32.of_int 0x88
    let tag_module = Int32.of_int 0x89
    let tag_module_name = Int32.of_int 0x8a
    let tag_symbol = Int32.of_int 0x8b

    let make_ebml_writer f =
        { wr_file = f; wr_stack = Stack.create() }

    let read_vint f =
        let b0 = input_byte f in

        let b, size = ref b0, ref 0 in
        let n, raw_n = ref(Int32.of_int(b0 land 0x7f)), ref(Int32.of_int b0) in
        while ((!b) land 0x80) != 0x80 do
            b := !b lsl 1;
            n := Int32.logand !n (Int32.shift_right (Int32.of_int 0x80) !size);
            incr size
        done;

        for i = 0 to !size - 1 do
            let b = Int32.of_int(input_byte f) in
            n := (Int32.logor (Int32.shift_left !n 8) b);
            raw_n := (Int32.logor (Int32.shift_left !raw_n 8) b)
        done;

        !n, !raw_n

    let make_writer f =
        { wr_file = f; wr_stack = Stack.create() }

    let start_tag writer tag_id =
        let tag_id = Int32.to_int tag_id in
        assert(tag_id < 0x100); (* increase me if needed later *)
        output_byte writer.wr_file tag_id;
        Stack.push (pos_out writer.wr_file) writer.wr_stack;
        output_binary_int writer.wr_file 0

    let end_tag writer =
        assert(not(Stack.is_empty writer.wr_stack));
        let end_pos = pos_out writer.wr_file in
        let start_pos = Stack.pop writer.wr_stack in
        seek_out writer.wr_file start_pos;

        let size = end_pos - start_pos - 4 in
        assert(size < 1 lsl 28);
        List.iter (output_byte writer.wr_file)
            [ 0x10 lor (size lsr 24); size lsr 16; size lsr 8; size ];
        seek_out writer.wr_file end_pos
end

let get_modules f =
    (* Read until we get to the MEMORY_MAP element. *)
    while (snd (EBML.read_vint f)) <> EBML.tag_memory_map do
        (* Skip over size. *)
        let size = Int32.to_int(fst(EBML.read_vint f)) in
        let pos = pos_in f in
        seek_in f (pos + size);
    done;
    ignore(EBML.read_vint f);

    (* Now look for each MEMORY_REGION element. *)
    let regions = DynArray.create() in
    while (snd (EBML.read_vint f)) = EBML.tag_memory_region do
        let size = Int32.to_int(fst(EBML.read_vint f)) in
        let pos = pos_in f in

        let in_io = IO.input_channel f in
        let region_start = IO.BigEndian.read_real_i32 in_io in
        let region_end = IO.BigEndian.read_real_i32 in_io in
        let region_offset = IO.BigEndian.read_real_i32 in_io in

        let region_path = IO.read_string in_io in
        let region_name = ExtList.List.last
            (ExtString.String.nsplit region_path "/") in

        seek_in f (pos + size);

        let region = {
            mr_start = region_start;
            mr_end = region_end;
            mr_offset = region_offset;
            mr_name = region_name;
            mr_path = region_path
        } in
        DynArray.add regions region
    done;
    DynArray.to_array regions

let get_cache_dirs binfo =
    let fennec_path =
        Printf.sprintf
            "%s/.piranha/symbol-cache/fennec/%s-%s-%s"
            (Unix.getenv "HOME")
            binfo.bi_name
            binfo.bi_version
            binfo.bi_build_id in
    mkdir_p fennec_path;

    let adb = Unix.open_process_in "adb get-serialno" in
    let syslibs_path =
        Std.finally (fun() -> ignore(Unix.close_process_in adb)) begin fun() ->
            Printf.sprintf
                "%s/.piranha/symbol-cache/syslibs/%s"
                (Unix.getenv "HOME")
                (input_line adb)
        end () in
    mkdir_p syslibs_path;

    { ca_fennec = fennec_path; ca_syslibs = syslibs_path }

(*
 *  Symbol retrieval
 *)

let fetch_fennec_symbols cache_dir symbol_urls module_name =
    if Hashtbl.mem symbol_urls module_name then begin
        let path = Printf.sprintf "%s/%s.syms" cache_dir module_name in
        begin
            try
                ignore(Unix.stat path);
                Printf.eprintf "Found cached symbols for '%s'\n" module_name;
                flush stderr
            with _ ->
                Printf.eprintf "Fetching symbols for '%s'..." module_name;
                flush stderr;
                let url = Hashtbl.find symbol_urls module_name in

                let curl = Curl.init() in
                Std.finally (fun() -> Curl.cleanup curl) begin fun() ->
                    let outf = open_out path in
                    Std.finally (fun() -> close_out outf) begin fun() ->
                        Curl.set_writefunction curl begin fun s ->
                            output_string outf s; String.length s
                        end;
                        Curl.set_url curl url;
                        Curl.perform curl
                    end ()
                end ();

                prerr_endline "done.";
                flush stderr
        end;
        Some (path, `Mozilla)
    end else begin
        Printf.eprintf "No symbols found for '%s'\n" module_name;
        flush stderr;
        None
    end

let fetch_syslib_symbols cache_dir module_path =
    (* Remove the leading "system/". *)
    let relpath = ExtString.String.strip ~chars:"/" module_path in
    let _, relpath = ExtString.String.split relpath "/" in
    let dest_path = Filename.concat cache_dir relpath in

    begin
        try
            ignore(Unix.stat dest_path);
            Printf.eprintf "Found cached system symbols for '%s'\n"
                module_path;
            flush stderr;
            Some (dest_path, `ELF)
        with _ ->
            mkdir_p (Filename.dirname dest_path);

            Printf.eprintf "Fetching system library '%s' from device..."
                module_path;
            flush stderr;

            let cmdline = Printf.sprintf "adb pull %s %s" module_path
                dest_path in
            if (Unix.system cmdline) = (Unix.WEXITED 0) then begin
                prerr_endline "done.";
                flush stderr;
                Some (dest_path, `ELF)
            end else begin
                prerr_endline "failed.";
                flush stderr;
                None
            end
    end

let fetch_symbols sources mregion =
    try
        (* Check the user's binary path first. *)
        match sources.ss_binary_path with
        | None -> raise Exit
        | Some path ->
            let path = Filename.concat path mregion.mr_name in
            Printf.eprintf "Looking for %s\n" path;
            ignore(Unix.stat path);
            Some (path, `ELF)
    with _ ->
        if ExtString.String.starts_with mregion.mr_path "/dev/ashmem" then
            (* Fennec library. *)
            fetch_fennec_symbols sources.ss_cache_dirs.ca_fennec
                sources.ss_symbol_urls mregion.mr_name
        else if ExtString.String.starts_with mregion.mr_path "/system" then
            (* Device standard library. *)
            fetch_syslib_symbols sources.ss_cache_dirs.ca_syslibs
                mregion.mr_path
        else begin
            Printf.eprintf
                "Don't know how to find symbols for '%s'\n"
                mregion.mr_path;
            None
        end

(*
 *  Symbol writing
 *)

let write_mozilla_symbols writer symbols_path =
    let f = open_in symbols_path in
    Std.finally (fun() -> close_in f) begin fun() ->
        let io = IO.output_channel writer.EBML.wr_file in
        let i = ref 0 in
        try
            while true do
                let line = input_line f in
                if ExtString.String.starts_with line "FUNC " then begin
                    let fields = ExtString.String.nsplit line " " in
                    let addr_str = List.nth fields 1 in
                    let symbol = ExtList.List.last fields in
                    let addr = int_of_string ("0x" ^ addr_str) in
                    EBML.start_tag writer EBML.tag_symbol;
                    IO.BigEndian.write_i32 io addr;
                    IO.write_string io symbol;
                    EBML.end_tag writer
                end;
                incr i;
                if !i mod 100000 = 0 then begin
                    prerr_char '.';
                    flush stderr
                end
            done
        with End_of_file -> ()
    end ()

let write_elf_symbols writer symbols_path dynamic =
    let cmd_line =
        Printf.sprintf
            "arm-eabi-nm%s -C %s"
            (if dynamic then " -D" else "")
            symbols_path in
    let nm = Unix.open_process_in cmd_line in
    Std.finally (fun() -> ignore(Unix.close_process_in nm)) begin fun() ->
        let io = IO.output_channel writer.EBML.wr_file in
        let i = ref 0 in
        try
            while true do
                let line = input_line nm in
                let fields = ExtString.String.nsplit line " " in
                if ((List.length fields) >= 3) &&
                        (String.lowercase (List.nth fields 1)) = "t" then begin
                    EBML.start_tag writer EBML.tag_symbol;
                    let addr = int_of_string ("0x" ^ (List.hd fields)) in
                    IO.BigEndian.write_i32 io addr;
                    IO.write_string io (List.nth fields 2);
                    EBML.end_tag writer
                end;
                incr i;
                if !i mod 100000 = 0 then begin
                    prerr_char '.';
                    flush stderr
                end
            done;
        with End_of_file -> ()
    end ()

let write_symbols writer module_name (symbols_path, symbols_type) =
    Printf.eprintf "Writing symbols for '%s'..." module_name; flush stderr;

    (* Write the module header. *)
    let io = IO.output_channel writer.EBML.wr_file in
    EBML.start_tag writer EBML.tag_module;
    EBML.start_tag writer EBML.tag_module_name;
    IO.write_string io module_name;
    EBML.end_tag writer;

    begin
        match symbols_type with
        | `Mozilla -> write_mozilla_symbols writer symbols_path
        | `ELF ->
            write_elf_symbols writer symbols_path true;
            write_elf_symbols writer symbols_path false
    end;

    EBML.end_tag writer;
    prerr_endline "done."; flush stderr

let fetch_and_write_symbols writer (sources:symbol_sources) mregion =
    let symbols_path_opt = fetch_symbols sources mregion in
    Option.may (write_symbols writer mregion.mr_path) symbols_path_opt

let main() =
    Curl.global_init Curl.CURLINIT_GLOBALALL;
    let opts = get_program_options() in
    let inf = open_in_bin opts.po_input_path in
    let outf = open_out_bin opts.po_output_path in
    Std.finally (fun() -> close_in inf; close_out outf) begin fun() ->
        let modules = get_modules inf in

        (* Copy the input to the output (inefficiently). *)
        seek_in inf 0;
        begin
            try
                while true do
                    output_byte outf (input_byte inf)
                done
            with End_of_file -> ()
        end;

        (* Gather up a list of modules we want to find information for. *)
        let module_list = Hashtbl.create 0 in
        Array.iter (fun mr -> Hashtbl.replace module_list mr.mr_name mr)
            modules;

        (* Write the symbols header. *)
        let writer = EBML.make_ebml_writer outf in
        EBML.start_tag writer EBML.tag_symbols;

        (* Fetch the symbols we need. *)
        let binfo = get_build_info opts.po_application_ini in
        let sources = {
            ss_cache_dirs = get_cache_dirs binfo;
            ss_symbol_urls = get_symbol_urls binfo;
            ss_binary_path = opts.po_binary_path
        } in
        Hashtbl.iter
            (fun _ v -> fetch_and_write_symbols writer sources v)
            module_list;

        (* Finish up. *)
        EBML.end_tag writer
    end ()
;;

main()

