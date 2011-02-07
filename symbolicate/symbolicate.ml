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
    mr_name: string;
}

type program_options = {
    po_input_path: string;
    po_output_path: string;
    po_application_ini: string option;
}

let get_program_options() =
    let oparser =
        OptParse.OptParser.make
            ~usage:"%prog [options] INPUT-PATH OUTPUT-PATH"
            ~version:"0.1"
            () in
    let application_ini = OptParse.Opt.value_option "PATH" None
        Std.identity (fun e s -> s) in
    OptParse.OptParser.add
        oparser
        ~help:"application.ini file for Fennec"
        ~short_name:'a'
        ~long_name:"application-ini"
        application_ini;
    let remaining_args = OptParse.OptParser.parse_argv oparser in
    if List.length remaining_args <> 2 then begin
        OptParse.OptParser.usage oparser ();
        exit 1
    end;
    {
        po_input_path = List.hd remaining_args;
        po_output_path = List.nth remaining_args 1;
        po_application_ini = OptParse.Opt.opt application_ini;
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
    Printf.eprintf "Fetching %s..." url;
    let response = Http_client.Convenience.http_get url in
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
    let size, _ = EBML.read_vint f in

    (* Now look for each MEMORY_REGION element. *)
    let regions = DynArray.create() in
    while (snd (EBML.read_vint f)) = EBML.tag_memory_region do
        let size = Int32.to_int(fst(EBML.read_vint f)) in
        let pos = pos_in f in

        let in_io = IO.input_channel f in
        let region_start = IO.BigEndian.read_real_i32 in_io in
        let region_end = IO.BigEndian.read_real_i32 in_io in

        let region_name = IO.read_string in_io in
        let region_name = ExtList.List.last
            (ExtString.String.nsplit region_name "/") in

        seek_in f (pos + size);

        let region = {
            mr_start = region_start;
            mr_end = region_end;
            mr_name = region_name
        } in
        DynArray.add regions region
    done;
    DynArray.to_array regions

let get_cache_dir binfo =
    let path =
        Printf.sprintf
            "%s/.piranha/symbol-cache/%s-%s-%s"
            (Unix.getenv "HOME")
            binfo.bi_name
            binfo.bi_version
            binfo.bi_build_id in
    (* mkdir -p *)
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
    end;
    path

let fetch_symbols cache_dir symbol_urls module_name =
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
                let msg = new Http_client.get url in
                msg#set_response_body_storage (`File (fun() -> path));
                let pipeline = new Http_client.pipeline in
                pipeline#add msg;
                pipeline#run();
                assert(msg#status = `Successful);
                prerr_endline "done.";
                flush stderr
        end;
        Some path
    end else begin
        Printf.eprintf "No symbols found for '%s'\n" module_name;
        flush stderr;
        None
    end

let write_symbols writer module_name symbols_path =
    let f = open_in symbols_path in
    Std.finally (fun() -> close_in f) begin fun() ->
        let io = IO.output_channel writer.EBML.wr_file in

        (* Write the module header. *)
        EBML.start_tag writer EBML.tag_module;
        EBML.start_tag writer EBML.tag_module_name;
        IO.write_string io module_name;
        EBML.end_tag writer;

        Printf.eprintf "Writing symbols for '%s'..." module_name; flush stderr;

        let i = ref 0 in
        begin
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
        end;
        prerr_endline "done."; flush stderr;

        EBML.end_tag writer
    end ()

let fetch_and_write_symbols writer cache_dir symbol_urls module_name =
    let symbols_path_opt = fetch_symbols cache_dir symbol_urls module_name in
    Option.may (write_symbols writer module_name) symbols_path_opt

let main() =
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

        (* Gather up a list of modules we want to find symbols for. *)
        let module_list = Hashtbl.create 0 in
        Array.iter
            (fun mr -> Hashtbl.replace module_list mr.mr_name ())
            modules;

        (* Write the symbols header. *)
        let writer = EBML.make_ebml_writer outf in
        EBML.start_tag writer EBML.tag_symbols;

        (* Fetch the symbols we need. *)
        let binfo = get_build_info opts.po_application_ini in
        let cache_dir = get_cache_dir binfo in
        let symbol_urls = get_symbol_urls binfo in
        Hashtbl.iter
            (fun k _ -> fetch_and_write_symbols writer cache_dir symbol_urls k)
            module_list;

        (* Finish up. *)
        EBML.end_tag writer
    end ()
;;

main()

