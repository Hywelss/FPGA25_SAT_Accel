`timescale 1ns/1ps

module clause_store_rtl_closed_loop_tb;
    logic ap_clk = 0;
    logic ap_rst_n = 0;
    logic ap_start = 0;
    wire ap_done;
    wire ap_idle;
    wire ap_ready;

    wire [4:0] clauseStore_address0;
    wire clauseStore_ce0;
    logic [127:0] clauseStore_q0;
    wire [3:0] commands_address0;
    wire commands_ce0;
    logic [63:0] commands_q0;
    wire [3:0] compactClauseLayout_address0;
    wire compactClauseLayout_ce0;
    logic compactClauseLayout_q0;

    logic [95:0] input1_TDATA = 0;
    logic input1_TVALID = 0;
    wire input1_TREADY;
    logic [95:0] input2_TDATA = 0;
    logic input2_TVALID = 0;
    wire input2_TREADY;
    wire [31:0] output1_TDATA;
    wire output1_TVALID;
    logic output1_TREADY = 0;
    wire [31:0] output2_TDATA;
    wire output2_TVALID;
    logic output2_TREADY = 0;

    logic [127:0] clauseStore [0:31];
    logic [63:0] commands [0:15];
    logic compactClauseLayout [0:15];
    integer errors = 0;
    integer accepted1 = 0;
    integer accepted2 = 0;
    integer produced1 = 0;
    integer produced2 = 0;

    always #2 ap_clk = ~ap_clk;

    always_ff @(posedge ap_clk) begin
        if(input1_TVALID && input1_TREADY)
            accepted1 <= accepted1 + 1;
        if(input2_TVALID && input2_TREADY)
            accepted2 <= accepted2 + 1;
        if(output1_TVALID && output1_TREADY)
            produced1 <= produced1 + 1;
        if(output2_TVALID && output2_TREADY)
            produced2 <= produced2 + 1;
        if(clauseStore_ce0)
            clauseStore_q0 <= clauseStore[clauseStore_address0];
        if(commands_ce0)
            commands_q0 <= commands[commands_address0];
        if(compactClauseLayout_ce0)
            compactClauseLayout_q0 <= compactClauseLayout[compactClauseLayout_address0];
    end

    clauseStoreReadCosim dut (
        .ap_clk, .ap_rst_n, .ap_start, .ap_done, .ap_idle, .ap_ready,
        .clauseStore_address0, .clauseStore_ce0, .clauseStore_q0,
        .commands_address0, .commands_ce0, .commands_q0,
        .compactClauseLayout_address0, .compactClauseLayout_ce0,
        .compactClauseLayout_q0,
        .clausePageSize(32'd8),
        .input1_TDATA, .input1_TVALID, .input1_TREADY,
        .input1_TKEEP(12'hfff), .input1_TSTRB(12'hfff), .input1_TLAST(1'b0),
        .input2_TDATA, .input2_TVALID, .input2_TREADY,
        .input2_TKEEP(12'hfff), .input2_TSTRB(12'hfff), .input2_TLAST(1'b0),
        .output1_TDATA, .output1_TVALID, .output1_TREADY,
        .output1_TKEEP(), .output1_TSTRB(), .output1_TLAST(),
        .output2_TDATA, .output2_TVALID, .output2_TREADY,
        .output2_TKEEP(), .output2_TSTRB(), .output2_TLAST()
    );

    task automatic send1(input logic [31:0] clauseID, input logic [31:0] code);
        begin
            @(negedge ap_clk);
            input1_TDATA = {code, 32'd0, clauseID};
            input1_TVALID = 1;
            do @(posedge ap_clk); while(!input1_TREADY);
            @(negedge ap_clk);
            input1_TVALID = 0;
        end
    endtask

    task automatic send2(input logic [31:0] clauseID, input logic [31:0] code);
        begin
            @(negedge ap_clk);
            input2_TDATA = {code, 32'd0, clauseID};
            input2_TVALID = 1;
            do @(posedge ap_clk); while(!input2_TREADY);
            @(negedge ap_clk);
            input2_TVALID = 0;
        end
    endtask

    task automatic receive1(input logic [31:0] expected, input integer delayCycles);
        begin
            output1_TREADY = 0;
            repeat(delayCycles) @(posedge ap_clk);
            @(negedge ap_clk);
            output1_TREADY = 1;
            do @(posedge ap_clk); while(!output1_TVALID);
            if(output1_TDATA !== expected) begin
                $display("lane 1 mismatch: expected %0d, got %0d", expected, output1_TDATA);
                errors = errors + 1;
            end
            @(negedge ap_clk);
            output1_TREADY = 0;
        end
    endtask

    task automatic receive2(input logic [31:0] expected, input integer delayCycles);
        begin
            output2_TREADY = 0;
            repeat(delayCycles) @(posedge ap_clk);
            @(negedge ap_clk);
            output2_TREADY = 1;
            do @(posedge ap_clk); while(!output2_TVALID);
            if(output2_TDATA !== expected) begin
                $display("lane 2 mismatch: expected %0d, got %0d", expected, output2_TDATA);
                errors = errors + 1;
            end
            @(negedge ap_clk);
            output2_TREADY = 0;
        end
    endtask

    task automatic clause1_0;
        begin
            send1(0, 0);
            receive1(11, 17); receive1(12, 5); receive1(13, 5);
            receive1(14, 5); receive1(15, 5); receive1(0, 5);
        end
    endtask

    task automatic clause1_2;
        begin
            send1(2, 0);
            receive1(31, 23); receive1(0, 7);
        end
    endtask

    task automatic clause1_4;
        begin
            send1(4, 0);
            receive1(51, 19); receive1(52, 5); receive1(53, 5);
            receive1(54, 5); receive1(55, 5); receive1(56, 5);
            receive1(57, 5); receive1(58, 5); receive1(0, 5);
        end
    endtask

    task automatic clause2_1;
        begin
            send2(1, 0);
            receive2(21, 2); receive2(22, 1); receive2(23, 1);
            receive2(24, 1); receive2(25, 1); receive2(26, 1);
            receive2(27, 1); receive2(28, 1); receive2(29, 1);
            receive2(0, 1);
        end
    endtask

    task automatic clause2_3;
        begin
            send2(3, 0);
            receive2(41, 11); receive2(42, 1); receive2(43, 1);
            receive2(44, 1); receive2(45, 1); receive2(46, 1);
            receive2(47, 1); receive2(0, 1);
        end
    endtask

    task automatic clause2_5;
        begin
            send2(5, 0);
            receive2(61, 3); receive2(62, 1); receive2(63, 1);
            receive2(64, 1); receive2(65, 1); receive2(66, 1);
            receive2(0, 1);
        end
    endtask

    task automatic startTransaction;
        begin
            // Start only after the previous ap_ctrl_hs transaction has fully
            // returned to idle.  Pulsing in the ap_done transition cycle can
            // be lost, while holding through ap_ready starts an unintended
            // back-to-back transaction.
            wait(ap_idle);
            @(negedge ap_clk);
            ap_start = 1;
            @(posedge ap_clk);
            @(negedge ap_clk);
            ap_start = 0;
        end
    endtask

    initial begin : timeout_guard
        repeat(20000) @(posedge ap_clk);
        $display("timeout state: start=%b done=%b idle=%b ready=%b", ap_start,
            ap_done, ap_idle, ap_ready);
        $display("lane1: in_valid=%b in_ready=%b out_valid=%b out_ready=%b out=%0d",
            input1_TVALID, input1_TREADY, output1_TVALID, output1_TREADY,
            output1_TDATA);
        $display("lane2: in_valid=%b in_ready=%b out_valid=%b out_ready=%b out=%0d",
            input2_TVALID, input2_TREADY, output2_TVALID, output2_TREADY,
            output2_TDATA);
        $display("handshakes: accepted=%0d/%0d produced=%0d/%0d", accepted1,
            accepted2, produced1, produced2);
        $fatal(1, "closed-loop RTL test timed out");
    end

    initial begin : test
        integer i;
        for(i = 0; i < 32; i = i + 1)
            clauseStore[i] = 0;
        for(i = 0; i < 8; i = i + 1) begin
            commands[i] = 0;
            compactClauseLayout[i] = 0;
        end

        commands[0] = {32'd5, 32'd0};
        compactClauseLayout[0] = 1;
        clauseStore[0] = {32'd4, 32'd13, 32'd12, 32'd11};
        clauseStore[1] = {64'd0, 32'd15, 32'd14};

        commands[1] = {32'd9, 32'd16};
        clauseStore[4] = {32'd24, 32'd23, 32'd22, 32'd21};
        clauseStore[5] = {32'd24, 32'd27, 32'd26, 32'd25};
        clauseStore[6] = {64'd0, 32'd29, 32'd28};

        commands[2] = {32'd1, 32'd32};
        compactClauseLayout[2] = 1;
        clauseStore[8] = 32'd31;

        commands[3] = {32'd7, 32'd40};
        clauseStore[10] = {32'd44, 32'd43, 32'd42, 32'd41};
        clauseStore[11] = {32'd0, 32'd47, 32'd46, 32'd45};

        commands[4] = {32'd8, 32'd48};
        clauseStore[12] = {32'd54, 32'd53, 32'd52, 32'd51};
        clauseStore[13] = {32'd64, 32'd57, 32'd56, 32'd55};
        clauseStore[16] = 32'd58;

        commands[5] = {32'd6, 32'd72};
        compactClauseLayout[5] = 1;
        clauseStore[18] = {32'd80, 32'd63, 32'd62, 32'd61};
        clauseStore[20] = {32'd0, 32'd66, 32'd65, 32'd64};

        commands[6] = {32'd1, 32'd1048576};
        commands[7] = {32'd4, 32'd88};
        compactClauseLayout[7] = 1;
        clauseStore[22] = {32'd1048576, 32'd73, 32'd72, 32'd71};

        repeat(5) @(posedge ap_clk);
        ap_rst_n = 1;
        repeat(3) @(posedge ap_clk);

        fork
            startTransaction();
            begin
                clause1_4(); clause1_2(); clause1_0();
                send1(0, 32'hffffffff);
            end
            begin
                clause2_1(); clause2_3(); clause2_5();
                send2(0, 32'hffffffff);
            end
        join
        wait(ap_done);
        @(posedge ap_clk);

        // Run the same RTL transaction again with lane 1 exiting immediately.
        fork
            startTransaction();
            send1(0, 32'hffffffff);
            begin
                clause2_2_reuse();
                send2(0, 32'hffffffff);
            end
        join
        wait(ap_done);

        // Invalid IDs must terminate as empty clauses instead of indexing
        // outside command memory and turning into an unbounded read.
        fork
            startTransaction();
            begin
                send1(32'hffffffff, 0);
                receive1(0, 13);
                send1(0, 32'hffffffff);
            end
            send2(0, 32'hffffffff);
        join
        wait(ap_done);

        // Invalid metadata and an invalid next-page link must each close the
        // clause response cleanly, even while the consumer applies backpressure.
        fork
            startTransaction();
            begin
                send1(6, 0);
                receive1(0, 31);
                send1(7, 0);
                receive1(71, 17); receive1(72, 7); receive1(73, 11);
                receive1(0, 29);
                send1(0, 32'hffffffff);
            end
            send2(0, 32'hffffffff);
        join
        wait(ap_done);

        if(errors != 0)
            $fatal(1, "closed-loop RTL test failed with %0d mismatches", errors);
        $display("CLOSED_LOOP_RTL_PASS");
        $finish;
    end

    task automatic clause2_2_reuse;
        begin
            send2(2, 0);
            receive2(31, 29); receive2(0, 7);
        end
    endtask
endmodule
