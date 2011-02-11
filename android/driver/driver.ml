(*
 *  piranha/android/driver/driver.ml
 *
 *  Copyright (c) 2011 Mozilla Foundation
 *  Patrick Walton <pcwalton@mozilla.com>
 *)

exception Found of int

type prog_opts = {
    po_piranha_path: string;
    po_app_id: string;
    po_task_name: string option;
}

let get_options() =
    let oparser =
        OptParse.OptParser.make
            ~usage:"%prog [options] PATH-TO-PIRANHA APP-ID"
            ~version:"0.1"
            () in
    let task_name = OptParse.Opt.value_option "PATH" None Std.identity
        (fun e s -> s) in
    OptParse.OptParser.add
        oparser
        ~help:"name of the task to profile (substrings ok)"
        ~short_name:'t'
        ~long_name:"task-name"
        task_name;
    let rest = OptParse.OptParser.parse_argv oparser in
    if (List.length rest) <> 2 then begin
        OptParse.OptParser.usage oparser ();
        exit 1
    end;
    {
        po_piranha_path = List.hd rest;
        po_app_id = List.nth rest 1;
        po_task_name = OptParse.Opt.opt task_name
    }

let psgrep pred =
    let f = Unix.open_process_in "adb shell ps" in
    Std.finally (fun() -> close_in f) begin fun() ->
        try
            while true do
                let line = ExtString.String.strip(input_line f) in
                let fields = ExtString.String.nsplit line " " in
                let fields = List.filter ((<>) "") fields in
                if pred (ExtList.List.last fields) then
                    raise(Found(int_of_string(List.nth fields 1)))
            done;
            failwith "unreachable"
        with Found pid -> pid
    end ()

let get_pid app_id target_task_name =
    psgrep begin fun task_name ->
        match target_task_name with
        | None -> task_name = app_id
        | Some tn -> ExtString.String.exists task_name tn
    end

let main() =
    let opts = get_options() in

    let pid =
        try get_pid opts.po_app_id opts.po_task_name
        with End_of_file ->
            prerr_endline
                "Couldn't find a process with that ID; is it running?";
            exit 1 in

    let cmd_line = Printf.sprintf "adb push %s /tmp/piranha"
        opts.po_piranha_path in
    assert((Unix.system cmd_line) = (Unix.WEXITED 0));

    let cmd_line =
        Printf.sprintf
            "adb shell run-as %s /tmp/piranha -o /tmp/profile.ebml %d"
            opts.po_app_id
            pid in
    Printf.eprintf "Running: %s\nPress Return to stop.\n" cmd_line;
    flush stderr;

    let adb_pid =
        Unix.create_process
            "adb"
            [|
                "adb";
                "shell";
                "run-as";
                opts.po_app_id;
                "/tmp/piranha";
                "-o";
                "/tmp/profile.ebml";
                (string_of_int pid)
            |]
            Unix.stdin
            Unix.stdout
            Unix.stderr in

    ignore(input_line stdin);
    (* FIXME: This could be wrong if the phone is running another app named
     * after a carnivorous fish. *)
    let piranha_pid =
        psgrep begin fun task_name ->
            (ExtString.String.exists task_name "piranha") &&
                (not(ExtString.String.exists task_name "run-as"))
        end in

    (* Send a SIGINT... *)
    let cmd_line =
        Printf.sprintf
            "adb shell run-as %s kill -2 %d"
            opts.po_app_id
            piranha_pid in
    Printf.eprintf "Running: %s\n" cmd_line;
    flush stderr;
    assert((Unix.system cmd_line) = (Unix.WEXITED 0));

    (* And wait. *)
    assert(snd(Unix.waitpid [] adb_pid) = (Unix.WEXITED 0));

    let cmd_line = "adb pull /tmp/profile.ebml profile.ebml" in
    assert((Unix.system cmd_line) = (Unix.WEXITED 0))
;;

main()

