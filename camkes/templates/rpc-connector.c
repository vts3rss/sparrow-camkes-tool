/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

/*# This defines a 'connector' that attempts to have an coherent API that can be implemented
  # differently by different connectors to change how data/control transfer happens between
  # components. By using the public interfaces defined here a template need only concern
  # itself with particular details of doing the marshalling/unmarshalling with the component
  # and leave the connector implementation free to determine how to communicate between
  # components. This connector specifically uses synchronous endpoints with Call+ReplyRecv,
  # but it could use shared memory and notifications just as easily without changing the
  # specific interfaces that use this.
  # Currently this connector has some rough edges and should potentially be split into two
  # connectors as it tries to support both dataport message passing as well as IPC buffer
  # message passing.
  #
  # For state the connectors rely on a namespace being given that they instantiate and then
  # should be given in every subsequent invocation. *some* members of this namespace are
  # explicitly documented as being public and can be used directly by the template code,
  # others are internal and not guaranteed to exist or be consistent across different
  # connector instantiations.
  #
  # The individual methods document their interactions and requirements
  #*/

/*# *** Internal helpers *** #*/

/*- macro _allocate_badges(namespace) -*/
    /*# Find any badges that have been explicitly assigned for this connection. That
     *# is, any badge identifiers that are not valid for us to assign automatically
     *# to other ends.
      #*/
    /*- set namespace.badges = [] -*/
    /*- for e in me.parent.from_ends -*/
        /*- set badge_attribute = '%s_attributes' % e.interface.name -*/
        /*- set badge = configuration[e.instance.name].get(badge_attribute) -*/
        /*- if isinstance(badge, six.integer_types) -*/
            /*- do namespace.badges.append(badge) -*/
        /*- elif isinstance(badge, six.string_types) and re.match('\\d+$', badge) is not none -*/
            /*- do namespace.badges.append(int(badge)) -*/
        /*- elif badge is not none -*/
            /*? raise(TemplateError('%s.%s must be either an integer or string encoding an integer' % (e.instance.name, badge_attribute), configuration.settings_dict[e.instance.name][badge_attribute])) ?*/
        /*- else -*/
            /*- do namespace.badges.append(none) -*/
        /*- endif -*/
    /*- endfor -*/

    /*# Now fill in any missing badges, skipping any already assigned badge values #*/
    /*- set next = [1] -*/
    /*- for _ in me.parent.from_ends -*/
        /*- if namespace.badges[loop.index0] is none -*/
            /*- for _ in namespace.badges -*/
                /*- if next[0] in namespace.badges -*/
                    /*- do next.__setitem__(0, next[0] + 1) -*/
                /*- endif -*/
            /*- endfor -*/
            /*- do namespace.badges.__setitem__(loop.index0, next[0]) -*/
        /*- endif -*/
    /*- endfor -*/
/*- endmacro -*/

/*- macro _establish_buffer(namespace, buffer, recv) -*/
    /*- if namespace.language == 'c' -*/
        /*- if buffer is none -*/
            /*- set namespace.userspace_ipc = False -*/
            /*- set base = '((void*)&seL4_GetIPCBuffer()->msg[0])' -*/
            /*- set buffer_size = '(seL4_MsgMaxLength * sizeof(seL4_Word))' -*/
            /*- set namespace.lock = False -*/
        /*- else -*/
            /*- set namespace.userspace_ipc = True -*/
            /*- set base = buffer[0] -*/
            /*- set buffer_size = buffer[1] -*/
        /*- if not recv -*/
                /*- set lock = buffer[2] -*/
                /*- set namespace.lock = lock -*/
                /*- if lock -*/
                    /*- set namespace.lock_symbol = c_symbol() -*/
                    /*- set namespace.lock_ep = alloc('userspace_buffer_ep', seL4_EndpointObject, write=True, read=True) -*/
                    static volatile int /*? namespace.lock_symbol ?*/ = 1;
                /*- else -*/
                    /*- set namespace.lock = False -*/
                /*- endif -*/
            /*- endif -*/
        /*- endif -*/
    /*- elif namespace.language == 'cakeml' -*/
        (* TODO: Move the derive_eval_thm_bytes function to a common library location for other cakeml src to use *)
        fun derive_eval_thm_bytes for_eval v_name e len = let
            val th = get_ml_prog_state () |> get_thm
            val th = MATCH_MP ml_progTheory.ML_code_Dlet_var th
                   |> REWRITE_RULE [ml_progTheory.ML_code_env_def]
            val th = th |> CONV_RULE(RESORT_FORALL_CONV(sort_vars["e","s3"]))
                      |> SPEC e
            val st = th |> SPEC_ALL |> concl |> dest_imp |> #1 |> strip_comb |> #2 |> el 1
            val new_st = ``^st with refs := ^st.refs ++ [W8array (REPLICATE ^(numSyntax.term_of_int len) 0w)]``
            val goal = th |> SPEC new_st |> SPEC_ALL |> concl |> dest_imp |> fst
            val lemma = goal |> (EVAL THENC SIMP_CONV(srw_ss())[semanticPrimitivesTheory.state_component_equality])
            val v_thm = prove(mk_imp(lemma |> concl |> rand, goal),
            rpt strip_tac \\ rveq \\ match_mp_tac(#2(EQ_IMP_RULE lemma))
            \\ asm_simp_tac bool_ss [])
            |> GEN_ALL |> SIMP_RULE std_ss [] |> SPEC_ALL;
            val v_tm = v_thm |> concl |> strip_comb |> #2 |> last
            val v_def = define_abbrev false v_name v_tm
            in v_thm |> REWRITE_RULE [GSYM v_def] end

        /*- if buffer is not none -*/
            /*? raise(TemplateError('CakeML connector only supports using the IPC buffer')) ?*/
        /*- endif -*/
        /*- set base = 'ConInternal.ipcbuf' -*/
        /*- set buffer_size = 120 * 8 -*/
        (* Add 17 because the protocol with the ffi IPC functions requires 1 byte for
           success and two 8 byte words for data *)
        /*- set bsize = buffer_size + 17 -*/
        val ipcbuf_e = ``(App Aw8alloc [Lit (IntLit /*? bsize ?*/); Lit (Word8 0w)])``
        /*- set namespace.cakeml_reserved_buf = 17 -*/
        val eval_thm = derive_eval_thm false "ipcbuf_loc" ipcbuf_e;
        val eval_thm = derive_eval_thm_bytes false "ipcbuf_loc" ipcbuf_e /*? bsize ?*/;
        val _ = ml_prog_update (add_Dlet eval_thm "ipcbuf" []);
    /*- endif -*/
    /*- set namespace.send_buffer = base -*/
    /*- set namespace.recv_buffer = base -*/
    /*- set namespace.send_buffer_size = buffer_size -*/
    /*- set namespace.recv_buffer_size = buffer_size -*/
    /*- set namespace.recv_buffer_size_fixed = namespace.userspace_ipc -*/
/*- endmacro -*/

/*- macro _make_get_sender_id_symbol(namespace, interface_name) -*/
    /*- set namespace.badge_symbol = '%s_badge' % interface_name -*/
    #include <sel4/sel4.h>
    static seL4_Word /*? namespace.badge_symbol ?*/ = 0;

    seL4_Word /*? interface_name ?*/_get_sender_id(void) {
        return /*? namespace.badge_symbol ?*/;
    }
/*- endmacro -*/

/*- macro _extract_size(namespace, info, size, recv) -*/
    /*? size ?*/ =
        /*- if namespace.userspace_ipc -*/
            /*- if recv -*/
                /*? namespace.recv_buffer_size ?*/
            /*- else -*/
                /*? namespace.send_buffer_size ?*/
            /*- endif -*/
        /*- else -*/
            seL4_MessageInfo_get_length(/*? info ?*/) * sizeof(seL4_Word);
            assert(/*? size ?*/ <= seL4_MsgMaxLength * sizeof(seL4_Word))
        /*- endif -*/
        ;
/*- endmacro -*/

/*- macro _save_reply_cap(namespace, might_block) -*/
    /*- if not options.realtime and might_block -*/
        /*# We need to save the reply cap because the user's implementation may
         * perform operations that overwrite or discard it.
         #*/
        /*? assert(namespace.reply_cap_slot is defined and namespace.reply_cap_slot > 0) ?*/
        /*- if namespace.language == 'c' -*/
            camkes_declare_reply_cap(/*? namespace.reply_cap_slot ?*/);
        /*- elif namespace.language == 'cakeml' -*/
            val _ = Utils.camkes_declare_reply_cap /*? namespace.reply_cap_slot ?*/;
        /*- endif -*/
    /*- endif -*/
/*- endmacro -*/

/*- macro _begin_cakeml_module(namespace) -*/
    val _ = ml_prog_update (open_module "ConInternal")
/*- endmacro -*/

/*- macro _end_cakeml_module(namespace) -*/
    val _ = ml_prog_update (close_module NONE);
/*- endmacro -*/

/*# *** Public interfaces *** #*/

/*# Instantiates a 'from' side of this connector for doing RPC using the 'default'
  # memory sharing policy, or the dataport if one is given.
  # This may generate symbols and other globals and should appear in the same namespace
  # as the rest of the instantiated template.
  # Will produce the follow values in the namespace that may be referenced
  #  send_buffer: Buffer to marshal inputs into for sending
  #  send_buffer_size: Size of the send buffer
  #  recv_buffer: Buffer to unmarsh outputs from
  #  recv_buffer_size: Size of the recv buffer
  #  recv_buffer_size_fixed: If fixed a received message has an 'unknown' size as the entire buffer is always transfered
  #  badges: List of the badge assigned to each incoming edge of the connector
  #*/
/*- macro establish_from_rpc(namespace, trust, buffer=none) -*/
    /*- set namespace.language = 'c' -*/
    /*# Establish the buffer for message contents #*/
    /*? _establish_buffer(namespace, buffer, False) ?*/
    /*- set namespace.trust_partner = trust -*/

    /*# Ensure the endpoint is allocated #*/
    /*- set ep_obj = alloc_obj('ep', seL4_EndpointObject) -*/
    /*- set ep = alloc_cap('ep_%s' % me.interface.name, ep_obj, write=True, grant=True) -*/

    /*? _allocate_badges(namespace) ?*/
    /*# Badge our capability #*/
    /*- do cap_space.cnode[ep].set_badge(namespace.badges[me.parent.from_ends.index(me)]) -*/

    /*# Store the EP for later messaging #*/
    /*- set namespace.ep = ep -*/
/*- endmacro -*/

/*# Establish the recv side of this connector for doing RPC.
  # Has the same requirements as establish_from_rpc and produces the same namespace items
  #*/
/*- macro establish_recv_rpc(namespace, trust, interface_name, buffer=none, language='c') -*/
    /*- set namespace.language = language -*/
    /*- if namespace.language == 'cakeml' -*/
        /*? _begin_cakeml_module(namespace) ?*/
    /*- endif -*/
    /*# Establish the buffer for message contents #*/
    /*? _establish_buffer(namespace, buffer, True) ?*/
    /*- set namespace.trust_partner = trust -*/

    /*# Ensure the endpoint is allocated #*/
    /*- set ep_obj = alloc_obj('ep', seL4_EndpointObject) -*/
    /*- set namespace.ep = alloc_cap('ep_%s' % me.interface.name, ep_obj, read=True, write=True) -*/

    /*? _allocate_badges(namespace) ?*/

    /*- if language == 'c' -*/
        /*? _make_get_sender_id_symbol(namespace, interface_name) ?*/
    /*- elif language == 'cakeml' -*/
        /*- set namespace.badge_symbol = 'badge' -*/
    /*- endif -*/

    /*# Allocate reply cap #*/
    /*- if options.realtime -*/
            /*- set namespace.reply_cap_slot = alloc('reply_cap_slot', seL4_RTReplyObject) -*/
    /*- else -*/
        /*- if me.might_block() -*/
            /*# We're going to need a CNode cap in order to save our pending reply
             * caps in the future.
             #*/
            /*- set namespace.cnode = alloc_cap('cnode', my_cnode, write=True) -*/
            /*- set namespace.reply_cap_slot = alloc_cap('reply_cap_slot', None) -*/
        /*- endif -*/
    /*- endif -*/
    /*- if namespace.language == 'cakeml' -*/
    /*- endif -*/
    /*- if namespace.language == 'cakeml' -*/
        /*? _end_cakeml_module(namespace) ?*/
    /*- endif -*/
/*- endmacro -*/

/*# *** The following functions all generated *code* that must be executed *** #*/

/*# The code output by this macro must be *executed* once prior to the code generated by
  # any of the other messaging macros for the recv side. This is special as a connector
  # may need to do something special to setup for the first RPC
  # Otherwise this is same as begin_recv
  #*/
/*- macro recv_first_rpc(namespace, size, might_block, notify_cptr=none) -*/
    /*- if not options.realtime and might_block -*/
        /*- if namespace.language == 'cakeml' -*/
            val _ = #(set_tls_cnode_cap) "" (Utils.int_to_bytes /*? namespace.cnode ?*/ 8);
        /*- else -*/
            camkes_get_tls()->cnode_cap = /*? namespace.cnode ?*/;
        /*- endif -*/
    /*- endif -*/
    /*- if namespace.language == 'c' -*/
        /*- set info = c_symbol('info') -*/
        /*- if notify_cptr is not none -*/
            /* This interface has a passive thread, must let the control thread know before waiting */
            seL4_MessageInfo_t /*? info ?*/ = /*? generate_seL4_SignalRecv(options,
                                                                        notify_cptr,
                                                                        info, namespace.ep,
                                                                        '&%s' % namespace.badge_symbol,
                                                                        namespace.reply_cap_slot) ?*/;
        /*- else -*/
            /* This interface has an active thread, just wait for an RPC */
            seL4_MessageInfo_t /*? info ?*/ = /*? generate_seL4_Recv(options, namespace.ep,
                                                                    '&%s' % namespace.badge_symbol,
                                                                     namespace.reply_cap_slot) ?*/;
        /*- endif -*/
        /*? _extract_size(namespace, info, size, True) ?*/
    /*- elif namespace.language == 'cakeml' -*/
        /*- if notify_cptr is not none -*/
            /*? raise(TemplateError('CakeML connector does not support notification')) ?*/
        /*- endif -*/
        val (/*? size ?*/, /*? namespace.badge_symbol ?*/) =
            Utils.seL4_Recv /*? namespace.ep ?*/ ConInternal.ipcbuf;
    /*- endif -*/
    /*? _save_reply_cap(namespace, might_block) ?*/
/*- endmacro -*/

/*# Releases ownership of the recv buffer #*/
/*- macro complete_recv(namespace) -*/
    /*# nothing needs to be done for us #*/
/*- endmacro -*/

/*# Takes ownership of the send buffer #*/
/*- macro begin_reply(namespace) -*/
    /*# nothing needs to be done for us #*/
/*- endmacro -*/

/*# Releases ownership of the send buffer #*/
/*- macro complete_reply(namespace) -*/
    /*# nothing needs to be done for us #*/
/*- endmacro -*/

/*# Recieves a message storing its length into the 'size' symbol and takes ownership
  # of the recv buffer #*/
/*- macro begin_recv(namespace, size, might_block) -*/
    /*- set info = c_symbol('info') -*/
    seL4_MessageInfo_t /*? info ?*/ = /*? generate_seL4_Recv(options, namespace.ep,
                                                             '&%s' % namespace.badge_symbol,
                                                             namespace.reply_cap_slot) ?*/;
    /*? _extract_size(namespace, info, size, True) ?*/
    /*? _save_reply_cap(namespace, might_block) ?*/
/*- endmacro -*/

/*# Sends whatever message is in the send buffer with the given `length`, and then
  # does begin_recv. This implicitly does complete_reply #*/
/*- macro reply_recv(namespace, length, size, might_block) -*/
    /*- if namespace.language == 'c' -*/
        /*- set info = c_symbol('info') -*/
        seL4_MessageInfo_t /*? info ?*/ = seL4_MessageInfo_new(0, 0, 0, /* length */
            /*- if namespace.userspace_ipc -*/
                0
            /*- else -*/
                ROUND_UP_UNSAFE(/*? length ?*/, sizeof(seL4_Word)) / sizeof(seL4_Word)
            /*- endif -*/
        );

        /* Send the response */
        /*- if not options.realtime and might_block -*/
            /*- set tls = c_symbol() -*/
            camkes_tls_t * /*? tls ?*/ UNUSED = camkes_get_tls();
            assert(/*? tls ?*/ != NULL);
            if (/*? tls ?*/->reply_cap_in_tcb) {
                /*? tls ?*/->reply_cap_in_tcb = false;
                /*? info ?*/ = /*? generate_seL4_ReplyRecv(options, namespace.ep,
                                                        info,
                                                        '&%s' % namespace.badge_symbol,
                                                        namespace.reply_cap_slot) ?*/;
            } else {
                camkes_unprotect_reply_cap();
                seL4_Send(/*? namespace.reply_cap_slot ?*/, /*? info ?*/);
                /*? info ?*/ = /*? generate_seL4_Recv(options, namespace.ep,
                                                    '&%s' % namespace.badge_symbol,
                                                    namespace.reply_cap_slot) ?*/;
            }
        /*- elif options.realtime -*/
            /*? info ?*/ = /*? generate_seL4_ReplyRecv(options, namespace.ep,
                                                    info,
                                                    '&%s' % namespace.badge_symbol,
                                                    namespace.reply_cap_slot) ?*/;
        /*- else -*/
            /*? info ?*/ = /*? generate_seL4_ReplyRecv(options, namespace.ep,
                                                    info,
                                                    '&%s' % namespace.badge_symbol,
                                                    namespace.reply_cap_slot) ?*/;
        /*- endif -*/
        /*? _extract_size(namespace, info, size, True) ?*/
    /*- elif namespace.language == 'cakeml' -*/
        /*- if options.realtime -*/
            /*? raise(TemplateError('CakeML connector does not support realtime')) ?*/
        /*- endif -*/
        val (/*? size ?*/, /*? namespace.badge_symbol ?*/) =
        /*- if might_block -*/
                if Utils.clear_tls_reply_cap_in_tcb () then
                    Utils.seL4_ReplyRecv /*? namespace.ep ?*/ /*? length ?*/ ConInternal.ipcbuf
                else let
                    val _ = Utils.seL4_Send /*? namespace.reply_cap_slot ?*/ /*? length ?*/ ConInternal.ipcbuf;
                    in Utils.seL4_Recv /*? namespace.ep ?*/ ConInternal.ipcbuf end
        /*- else -*/
            Utils.seL4_ReplyRecv /*? namespace.ep ?*/ /*? length ?*/ ConInternal.ipcbuf
        /*- endif -*/
        ;
    /*- endif -*/
    /*? _save_reply_cap(namespace, might_block) ?*/
/*- endmacro -*/

/*# Takes ownership of the send buffer #*/
/*- macro begin_send(namespace) -*/
    /* Save any pending reply cap as we'll eventually call seL4_Call which
     * could overwrite it. Note that we do this here before marshalling
     * parameters to ensure we don't inadvertently overwrite any marshalled
     * data with this call.
     */
    /*- if not options.realtime -*/
        camkes_protect_reply_cap();
    /*- endif -*/
    /*- if namespace.lock -*/
        sync_sem_bare_wait(/*? namespace.lock_ep ?*/,
            &/*? namespace.lock_symbol ?*/);
    /*- endif -*/
/*- endmacro -*/

/*# Sends a message and receives a reply. Implicitly does complete_reply and
  # takes ownership of the recv buffer #*/
/*- macro perform_call(namespace, size, length) -*/
    /* Call the endpoint */
    /*- set info = c_symbol('info') -*/
    seL4_MessageInfo_t /*? info ?*/ = seL4_MessageInfo_new(0, 0, 0,
        /*- if namespace.userspace_ipc -*/
                0
        /*- else -*/
                ROUND_UP_UNSAFE(/*? length ?*/, sizeof(seL4_Word)) / sizeof(seL4_Word)
        /*- endif -*/
        );
    /*? info ?*/ = seL4_Call(/*? namespace.ep ?*/, /*? info ?*/);

    /*? size ?*/ =
    /*- if namespace.userspace_ipc -*/
        /*? namespace.recv_buffer_size ?*/;
    /*- else -*/
        seL4_MessageInfo_get_length(/*? info ?*/) * sizeof(seL4_Word);
        assert(/*? size ?*/ <= seL4_MsgMaxLength * sizeof(seL4_Word));
    /*- endif -*/
/*- endmacro -*/

/*- macro maybe_perform_optimized_empty_call(namespace) -*/
    #ifdef ARCH_ARM
    #ifndef __SWINUM
        #define __SWINUM(x) ((x) & 0x00ffffff)
    #endif
        /* We don't need to send or return any information because this
         * is the only method in this interface and it has no parameters or
         * return value. We can use an optimised syscall stub and take an
         * early exit.
         *
         * To explain where this stub deviates from the standard Call stub:
         *  - No asm clobbers because we're not receiving any arguments in
         *    the reply (that would usually clobber r2-r5);
         *  - Message info as an input only because we know the return info
         *    will be identical, so the compiler can avoid reloading it if
         *    we need the value after the syscall; and
         *  - Setup r7 and r1 first because they are preserved across the
         *    syscall and this helps the compiler emit a backwards branch
         *    to create a tight loop if we're calling this interface
         *    repeatedly.
         */
        /*- set scno = c_symbol() -*/
        register seL4_Word /*? scno ?*/ asm("r7") = seL4_SysCall;
        /*- set tag = c_symbol() -*/
        register seL4_MessageInfo_t /*? tag ?*/ asm("r1") = seL4_MessageInfo_new(0, 0, 0, 0);
        /*- set dest = c_symbol() -*/
        register seL4_Word /*? dest ?*/ asm("r0") = /*? namespace.ep ?*/;
        asm volatile("swi %[swinum]"
            /*- if namespace.trust_partner -*/
                :"+r"(/*? dest ?*/)
                :[swinum]"i"(__SWINUM(seL4_SysCall)), "r"(/*? scno ?*/), "r"(/*? tag ?*/)
            /*- else -*/
                :"+r"(/*? dest ?*/), "+r"(/*? tag ?*/)
                :[swinum]"i"(__SWINUM(seL4_SysCall)), "r"(/*? scno ?*/)
                :"r2", "r3", "r4", "r5", "memory"
            /*- endif -*/
        );
        return;
    #endif
/*- endmacro -*/

/*# Releases the recv buffer #*/
/*- macro release_recv(namespace) -*/
    /*- if namespace.lock -*/
        sync_sem_bare_post(/*? namespace.lock_ep ?*/,
            &/*? namespace.lock_symbol ?*/);
    /*- endif -*/
/*- endmacro -*/
