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

let get_build_info() =
    let cmd = "adb shell run-as org.mozilla.fennec cat application.ini" in
    let f = Unix.open_process_in cmd in
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
    let tag_symbol = Int32.of_int 0x8a

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
    List.fold_left begin fun pathname component ->
        let pathname = pathname ^ component ^ "/" in
        begin
            try Unix.mkdir pathname 0o775
            with Unix.Unix_error(Unix.EEXIST, _, _) -> ()
        end;
        pathname
    end "" (ExtString.String.nsplit path "/");
    path

let fetch_symbols cache_dir symbol_urls module_name =
    if Hashtbl.mem symbol_urls module_name then begin
        let path = Printf.sprintf "%s/%s.syms" cache_dir module_name in
        begin
            try
                ignore(Unix.stat path);
                Printf.eprintf "Found cached symbols for '%s'\n" module_name
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
                prerr_endline "done."
        end;
        Some path
    end else begin
        Printf.eprintf "No symbols found for '%s'\n" module_name;
        None
    end

let fetch_and_write_symbols outf cache_dir symbol_urls module_name =
    ignore(fetch_symbols cache_dir symbol_urls module_name)

let main() =
    if Array.length Sys.argv < 3 then begin
        prerr_endline "usage: piranha-symbolicate INPUT OUTPUT";
        exit 1
    end;
    let input_path, output_path = Sys.argv.(1), Sys.argv.(2) in
    let inf, outf = open_in_bin input_path, open_out_bin output_path in
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

        (* Fetch the symbols we need. *)
        let binfo = get_build_info() in
        let cache_dir = get_cache_dir binfo in
        let symbol_urls = get_symbol_urls binfo in
        Hashtbl.iter
            (fun k _ -> fetch_and_write_symbols outf cache_dir symbol_urls k)
            module_list
    end ()
;;

main()

