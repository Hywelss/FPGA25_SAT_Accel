`timescale 1ns/1ps

module deletion_closed_loop_rtl_tb;
    localparam integer MAX_LITERAL_ELEMENTS = 524288;
    localparam integer MAX_LITERALS = 32768;
    localparam integer PAGE_SIZE = 8;
    localparam integer NORMAL_ID = 11;
    localparam integer NORMAL_LENGTH = 1024;
    localparam integer SHORT_COUNT = 129;
    localparam integer REMOVE_TOTAL = SHORT_COUNT + 1;

    logic ap_clk = 0;
    logic ap_rst_n = 0;
    logic ap_start = 0;
    wire clause_done, clause_idle, clause_ready;
    wire manage_done, manage_idle, manage_ready;
    integer cycles = 0;
    integer output_words = 0;
    integer update_words = 0;
    integer id_index = 0;
    integer completed_records = 0;
    integer location_responses = 0;
    integer id_handshakes = 0;
    logic [15:0] lfsr = 16'h1ace;
    logic clause_done_seen;
    logic manage_done_seen;

    logic [63:0] clause_metadata [0:131071];
    logic [127:0] clause_store [0:131071];
    logic clause_layout [0:131071];
    logic [511:0] literal_store [0:32767];
    logic [417:0] literal_metadata [0:32767];
    logic [31:0] clause_to_literal [0:MAX_LITERAL_ELEMENTS-1];

    wire [16:0] mCmd_address0;
    wire mCmd_ce0, mCmd_we0;
    wire [63:0] mCmd_d0;
    logic [63:0] mCmd_q0;
    wire [16:0] mClsStore_address0;
    wire mClsStore_ce0;
    logic [127:0] mClsStore_q0;
    wire [16:0] compactClauseLayout_address0;
    wire compactClauseLayout_ce0;
    logic compactClauseLayout_q0;

    wire [14:0] literalStore_address0, literalStore_address1;
    wire literalStore_ce0, literalStore_ce1;
    wire literalStore_we1;
    wire [511:0] literalStore_d1;
    logic [511:0] literalStore_q0, literalStore_q1;
    wire [14:0] metadata_address0;
    wire metadata_ce0, metadata_we0;
    wire [417:0] metadata_d0;
    logic [417:0] metadata_q0;
    wire [31:0] freePageCount;

    wire [31:0] clause_output_data;
    wire clause_output_valid;
    wire clause_output_ready;
    wire [63:0] clause_location_data;
    wire clause_location_valid;
    wire clause_location_ready;
    wire [95:0] manage_update_data;
    wire manage_update_valid;
    wire manage_update_ready;

    logic frame_valid = 0;
    logic [31:0] frame_data = 0;
    logic count_pending = 1;
    wire frame_to_manage = frame_valid;
    wire frame_consumed;

    logic update_valid = 0;
    logic [95:0] update_data = 0;
    wire update_to_clause = update_valid;
    wire update_consumed;

    logic location_valid = 0;
    logic [31:0] location_data = 0;
    logic [1:0] location_mode = 0;
    wire location_to_manage = location_valid;
    wire location_consumed;
    wire location_is_payload = location_mode == 1 &&
        clause_location_data != 64'hffffffffffffffff;

    // Mirror the solver protocol: do not send the next ID until the current
    // clause has completed its transposed-view update transaction.
    wire id_valid = id_index < REMOVE_TOTAL && id_index <= completed_records;
    wire [31:0] id_data = id_index < SHORT_COUNT ? 100 + id_index : NORMAL_ID;
    wire id_ready;
    wire frame_ready;
    wire update_ready;
    wire location_ready;

    assign frame_consumed = frame_to_manage && frame_ready;
    assign clause_output_ready = !count_pending &&
        (!frame_valid || frame_consumed) && lfsr[0];
    assign update_consumed = update_to_clause && update_ready;
    assign manage_update_ready = (!update_valid || update_consumed) && lfsr[2];
    assign location_consumed = location_to_manage && location_ready;
    assign clause_location_ready = lfsr[4] &&
        (!location_is_payload || !location_valid || location_consumed);

    always #2.5 ap_clk = ~ap_clk;

    always_ff @(posedge ap_clk) begin
        if(!ap_rst_n) begin
            lfsr <= 16'h1ace;
            id_index <= 0;
            id_handshakes <= 0;
            completed_records <= 0;
            frame_valid <= 0;
            count_pending <= 1;
            update_valid <= 0;
            location_valid <= 0;
            location_mode <= 0;
            clause_done_seen <= 0;
            manage_done_seen <= 0;
        end else begin
            if(clause_done)
                clause_done_seen <= 1;
            if(manage_done)
                manage_done_seen <= 1;
            lfsr <= {lfsr[14:0], lfsr[15]^lfsr[13]^lfsr[12]^lfsr[10]};
            if(id_valid && id_ready) begin
                id_index <= id_index + 1;
                id_handshakes <= id_handshakes + 1;
                if(id_handshakes < 16)
                    $display("ID_TO_CLAUSE cycle=%0d data=%0d", cycles, id_data);
            end

            if(frame_consumed)
                frame_valid <= 0;
            if(frame_consumed && output_words < 16)
                $display("FRAME_TO_MANAGE cycle=%0d data=%0d", cycles, frame_data);
            if(count_pending && !frame_valid) begin
                frame_data <= REMOVE_TOTAL;
                frame_valid <= 1;
                count_pending <= 0;
            end else if(clause_output_valid && clause_output_ready) begin
                frame_data <= clause_output_data;
                frame_valid <= 1;
                output_words <= output_words + 1;
                if(output_words < 16)
                    $display("CLAUSE_TO_FRAME cycle=%0d data=%0d", cycles,
                        clause_output_data);
            end

            if(update_consumed)
                update_valid <= 0;
            if(manage_update_valid && manage_update_ready) begin
                update_data <= manage_update_data;
                update_valid <= 1;
                update_words <= update_words + 1;
                if(manage_update_data[95:64] == 32'hffffffff)
                    completed_records <= completed_records + 1;
                if(update_words < 16)
                    $display("MANAGE_TO_UPDATE cycle=%0d data=%h", cycles,
                        manage_update_data);
            end

            if(location_consumed)
                location_valid <= 0;
            if(clause_location_valid && clause_location_ready) begin
                if(location_mode == 0 && clause_location_data[31:0] == 1)
                    location_mode <= 1;
                else if(location_mode == 0 && clause_location_data[31:0] == 3)
                    location_mode <= 2;
                else if(clause_location_data == 64'hffffffffffffffff)
                    location_mode <= 0;
                else if(location_mode == 1) begin
                    location_data <= clause_to_literal[clause_location_data[31:0]];
                    location_valid <= 1;
                    location_responses <= location_responses + 1;
                end
            end

            if(mCmd_ce0) begin
                mCmd_q0 <= clause_metadata[mCmd_address0];
                if(mCmd_we0)
                    clause_metadata[mCmd_address0] <= mCmd_d0;
            end
            if(mClsStore_ce0)
                mClsStore_q0 <= clause_store[mClsStore_address0];
            if(compactClauseLayout_ce0)
                compactClauseLayout_q0 <= clause_layout[compactClauseLayout_address0];

            if(literalStore_ce0) begin
                literalStore_q0 <= literal_store[literalStore_address0];
            end
            if(literalStore_ce1) begin
                literalStore_q1 <= literal_store[literalStore_address1];
                if(literalStore_we1)
                    literal_store[literalStore_address1] <= literalStore_d1;
            end
            if(metadata_ce0) begin
                metadata_q0 <= literal_metadata[metadata_address0];
                if(metadata_we0)
                    literal_metadata[metadata_address0] <= metadata_d0;
            end
        end
    end

    clauseStoreDeleteCosim clause_dut (
        .ap_clk, .ap_rst_n, .ap_start, .ap_done(clause_done),
        .ap_idle(clause_idle), .ap_ready(clause_ready),
        .clauseStoreInputStream1_TDATA(update_data),
        .clauseStoreInputStream1_TVALID(update_to_clause),
        .clauseStoreInputStream1_TREADY(update_ready),
        .clauseStoreInputStream1_TKEEP(12'hfff),
        .clauseStoreInputStream1_TSTRB(12'hfff),
        .clauseStoreInputStream1_TLAST(1'b0),
        .clauseStoreInputStream2_TDATA({64'd0,id_data}),
        .clauseStoreInputStream2_TVALID(id_valid),
        .clauseStoreInputStream2_TREADY(id_ready),
        .clauseStoreInputStream2_TKEEP(12'hfff),
        .clauseStoreInputStream2_TSTRB(12'hfff),
        .clauseStoreInputStream2_TLAST(1'b0),
        .clauseStoreOutputStream1_TDATA(clause_output_data),
        .clauseStoreOutputStream1_TVALID(clause_output_valid),
        .clauseStoreOutputStream1_TREADY(clause_output_ready),
        .clauseStoreOutputStream1_TKEEP(), .clauseStoreOutputStream1_TSTRB(),
        .clauseStoreOutputStream1_TLAST(),
        .locationInputStream_TDATA(clause_location_data),
        .locationInputStream_TVALID(clause_location_valid),
        .locationInputStream_TREADY(clause_location_ready),
        .locationInputStream_TKEEP(), .locationInputStream_TSTRB(),
        .locationInputStream_TLAST(),
        .mCmd_address0, .mCmd_ce0, .mCmd_we0, .mCmd_d0, .mCmd_q0,
        .mClsStore_address0, .mClsStore_ce0, .mClsStore_q0,
        .compactClauseLayout_address0, .compactClauseLayout_ce0,
        .compactClauseLayout_q0, .removeTotal(REMOVE_TOTAL),
        .clausePageSize(PAGE_SIZE)
    );

    manageDeleteCosim manage_dut (
        .ap_clk, .ap_rst_n, .ap_start, .ap_done(manage_done),
        .ap_idle(manage_idle), .ap_ready(manage_ready),
        .updates_TDATA(manage_update_data),
        .updates_TVALID(manage_update_valid),
        .updates_TREADY(manage_update_ready),
        .updates_TKEEP(), .updates_TSTRB(), .updates_TLAST(),
        .deletedClauses_TDATA(frame_data),
        .deletedClauses_TVALID(frame_to_manage),
        .deletedClauses_TREADY(frame_ready),
        .deletedClauses_TKEEP(4'hf), .deletedClauses_TSTRB(4'hf),
        .deletedClauses_TLAST(1'b0),
        .locations_TDATA(location_data),
        .locations_TVALID(location_to_manage),
        .locations_TREADY(location_ready),
        .locations_TKEEP(4'hf), .locations_TSTRB(4'hf),
        .locations_TLAST(1'b0),
        .literalStore_address0, .literalStore_ce0, .literalStore_q0,
        .literalStore_address1, .literalStore_ce1, .literalStore_we1,
        .literalStore_d1, .literalStore_q1,
        .metadata_address0, .metadata_ce0, .metadata_we0,
        .metadata_d0, .metadata_q0,
        .freePageCount, .freePageCount_ap_vld()
    );

    initial begin : test
        integer i, page, offset, address, link_address, clause_id, variable;
        for(i = 0; i < 131072; i = i + 1) begin
            clause_metadata[i] = 0;
            clause_store[i] = 0;
            clause_layout[i] = 0;
        end
        for(i = 0; i < 32768; i = i + 1) begin
            literal_store[i] = 0;
            literal_metadata[i] = 0;
        end

        clause_metadata[21] = {32'd2,32'd524288};
        clause_metadata[22] = {32'd8,32'd2048};
        for(i = 0; i < 7; i = i + 1)
            clause_store[(2048+i)/4][32*((2048+i)%4) +: 32] = i+1;
        clause_store[(2048+PAGE_SIZE-1)/4][127:96] = 524288;
        clause_metadata[23] = {32'd1,32'd4096};

        clause_metadata[NORMAL_ID] = {NORMAL_LENGTH[31:0],32'd0};
        for(i = 0; i < NORMAL_LENGTH; i = i + 1) begin
            page = i/(PAGE_SIZE-1);
            offset = i%(PAGE_SIZE-1);
            address = page*PAGE_SIZE+offset;
            clause_store[address/4][32*(address%4) +: 32] = i+1;
            clause_to_literal[address] = i*16;
            if(offset == PAGE_SIZE-2 && i+1 < NORMAL_LENGTH) begin
                link_address = page*PAGE_SIZE+PAGE_SIZE-1;
                clause_store[link_address/4][32*(link_address%4) +: 32] =
                    (page+1)*PAGE_SIZE;
            end

            literal_store[i][31:0] = NORMAL_ID+1;
            literal_metadata[i][225:194] = 1;
            literal_metadata[i][161:130] = i*16;
            literal_metadata[i][97:66] = 13;
        end

        for(i = 0; i < SHORT_COUNT; i = i + 1) begin
            clause_id = 100 + i;
            variable = NORMAL_LENGTH + i;
            address = 16384 + i*PAGE_SIZE;
            clause_metadata[clause_id] = {32'd1,address[31:0]};
            clause_store[address/4][32*(address%4) +: 32] = variable + 1;
            clause_to_literal[address] = variable*16;
            literal_store[variable][31:0] = clause_id + 1;
            literal_metadata[variable][225:194] = 1;
            literal_metadata[variable][161:130] = variable*16;
            literal_metadata[variable][97:66] = 13;
        end

        repeat(8) @(posedge ap_clk);
        ap_rst_n = 1;
        repeat(5) @(posedge ap_clk);
        @(negedge ap_clk);
        ap_start = 1;
        @(posedge ap_clk);
        @(negedge ap_clk);
        ap_start = 0;

        while(!(clause_done_seen && manage_done_seen) && cycles < 1600000) begin
            @(posedge ap_clk);
            cycles = cycles + 1;
        end
        if(!(clause_done_seen && manage_done_seen)) begin
            $display("DELETION_CLOSED_LOOP_RTL_TIMEOUT cycles=%0d clause=%b/%b/%b manage=%b/%b/%b ids=%0d words=%0d updates=%0d locations=%0d",
                cycles, clause_done_seen, clause_idle, clause_ready,
                manage_done_seen, manage_idle, manage_ready, id_handshakes,
                output_words, update_words, location_responses);
            $display("DELETION_CLOSED_LOOP_RTL_STALL id_vr=%b%b clause_out_vr=%b%b frame_vr=%b%b manage_up_vr=%b%b update_vr=%b%b clause_loc_vr=%b%b location_vr=%b%b",
                id_valid, id_ready, clause_output_valid, clause_output_ready,
                frame_to_manage, frame_ready, manage_update_valid,
                manage_update_ready, update_to_clause, update_ready,
                clause_location_valid, clause_location_ready,
                location_to_manage, location_ready);
            $fatal(1);
            $finish;
        end
        if(output_words != NORMAL_LENGTH+2+3*SHORT_COUNT ||
                update_words != NORMAL_LENGTH+1+2*SHORT_COUNT ||
                location_responses != NORMAL_LENGTH+SHORT_COUNT ||
                completed_records != REMOVE_TOTAL) begin
            $display("DELETION_CLOSED_LOOP_RTL_BAD_COUNTS output=%0d updates=%0d locations=%0d",
                output_words, update_words, location_responses);
            $fatal(1);
            $finish;
        end
        if(clause_metadata[NORMAL_ID][63:32] != 0 ||
                literal_metadata[0][225:194] != 0 ||
                literal_metadata[NORMAL_LENGTH-1][225:194] != 0 ||
                clause_metadata[100][63:32] != 0 ||
                clause_metadata[100+SHORT_COUNT-1][63:32] != 0 ||
                literal_metadata[NORMAL_LENGTH][225:194] != 0 ||
                literal_metadata[NORMAL_LENGTH+SHORT_COUNT-1][225:194] != 0) begin
            $display("DELETION_CLOSED_LOOP_RTL_BAD_STATE clause=%0d first=%0d last=%0d",
                clause_metadata[NORMAL_ID][63:32],
                literal_metadata[0][225:194],
                literal_metadata[NORMAL_LENGTH-1][225:194]);
            $fatal(1);
            $finish;
        end
        $display("DELETION_CLOSED_LOOP_RTL_PASS cycles=%0d", cycles);
        $finish;
    end
endmodule
