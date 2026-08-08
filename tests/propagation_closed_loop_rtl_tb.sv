`timescale 1ns/1ps

module propagation_closed_loop_rtl_tb;
    logic ap_clk = 0;
    logic ap_rst_n = 0;
    logic ap_start = 0;
    wire ap_done;
    wire ap_idle;
    wire ap_ready;

    logic [31:0] clauseLengths_TDATA = 0;
    logic clauseLengths_TVALID = 0;
    wire clauseLengths_TREADY;
    logic [3:0] clauseLengths_TKEEP = 4'hf;
    logic [3:0] clauseLengths_TSTRB = 4'hf;
    logic clauseLengths_TLAST = 0;

    wire [95:0] clauseRequests_TDATA;
    wire clauseRequests_TVALID;
    logic clauseRequests_TREADY = 0;
    wire [11:0] clauseRequests_TKEEP;
    wire [11:0] clauseRequests_TSTRB;
    wire clauseRequests_TLAST;

    logic [31:0] scenario = 0;
    wire [31:0] answerHeight;
    wire answerHeight_ap_vld;
    wire [31:0] firstAnswer;
    wire firstAnswer_ap_vld;
    wire [31:0] secondAnswer;
    wire secondAnswer_ap_vld;
    wire doBacktrack;
    wire doBacktrack_ap_vld;
    wire [31:0] conflictCount;
    wire conflictCount_ap_vld;

    logic [15:0] lfsr = 16'h1ace;
    integer request_count = 0;
    integer length_count = 0;
    logic [31:0] last_request = 0;
    integer captured_height = -1;
    integer captured_first = 0;
    integer captured_second = 0;
    integer captured_backtrack = -1;
    integer captured_conflicts = -1;
    always #2.5 ap_clk = ~ap_clk;

    always @(posedge ap_clk) begin
        if(!ap_rst_n) begin
            lfsr <= 16'h1ace;
            clauseRequests_TREADY <= 0;
        end else begin
            lfsr <= {lfsr[14:0], lfsr[15] ^ lfsr[13] ^ lfsr[12] ^ lfsr[10]};
            clauseRequests_TREADY <= lfsr[0] | lfsr[3];
            if(clauseLengths_TVALID && clauseLengths_TREADY)
                length_count <= length_count + 1;
            if(clauseRequests_TVALID && clauseRequests_TREADY) begin
                request_count <= request_count + 1;
                last_request <= clauseRequests_TDATA[31:0];
            end
            if(answerHeight_ap_vld)
                captured_height <= answerHeight;
            if(firstAnswer_ap_vld)
                captured_first <= firstAnswer;
            if(secondAnswer_ap_vld)
                captured_second <= secondAnswer;
            if(doBacktrack_ap_vld)
                captured_backtrack <= doBacktrack;
            if(conflictCount_ap_vld)
                captured_conflicts <= conflictCount;
        end
    end

    propagationClosedLoopCosim dut (
        .ap_clk(ap_clk), .ap_rst_n(ap_rst_n), .ap_start(ap_start),
        .ap_done(ap_done), .ap_idle(ap_idle), .ap_ready(ap_ready),
        .clauseLengths_TDATA(clauseLengths_TDATA),
        .clauseLengths_TVALID(clauseLengths_TVALID),
        .clauseLengths_TREADY(clauseLengths_TREADY),
        .clauseLengths_TKEEP(clauseLengths_TKEEP),
        .clauseLengths_TSTRB(clauseLengths_TSTRB),
        .clauseLengths_TLAST(clauseLengths_TLAST),
        .clauseRequests_TDATA(clauseRequests_TDATA),
        .clauseRequests_TVALID(clauseRequests_TVALID),
        .clauseRequests_TREADY(clauseRequests_TREADY),
        .clauseRequests_TKEEP(clauseRequests_TKEEP),
        .clauseRequests_TSTRB(clauseRequests_TSTRB),
        .clauseRequests_TLAST(clauseRequests_TLAST),
        .scenario(scenario), .answerHeight(answerHeight),
        .answerHeight_ap_vld(answerHeight_ap_vld),
        .firstAnswer(firstAnswer), .firstAnswer_ap_vld(firstAnswer_ap_vld),
        .secondAnswer(secondAnswer), .secondAnswer_ap_vld(secondAnswer_ap_vld),
        .doBacktrack(doBacktrack),
        .doBacktrack_ap_vld(doBacktrack_ap_vld),
        .conflictCount(conflictCount),
        .conflictCount_ap_vld(conflictCount_ap_vld)
    );

    task automatic run_scenario(input integer selected);
        integer cycles;
        integer expected_lengths;
        integer scenario_matches;
        begin
            scenario = selected;
            request_count = 0;
            length_count = 0;
            last_request = 0;
            captured_height = -1;
            captured_first = 0;
            captured_second = 0;
            captured_backtrack = -1;
            captured_conflicts = -1;
            clauseLengths_TVALID = 0;
            clauseLengths_TDATA = 2;
            expected_lengths = (selected >= 9 && selected <= 12) ? 2 :
                ((selected == 1 || selected == 8) ? 1 : 0);

            if(expected_lengths != 0) begin
                fork
                    begin : length_response_driver
                        integer response;
                        for(response = 0; response < expected_lengths; response++) begin
                            @(negedge ap_clk);
                            clauseLengths_TDATA = 2;
                            clauseLengths_TVALID = 1;
                            wait(clauseLengths_TVALID && clauseLengths_TREADY);
                            @(posedge ap_clk);
                            @(negedge ap_clk);
                            clauseLengths_TVALID = 0;
                        end
                    end
                join_none
            end

            @(posedge ap_clk);
            ap_start <= 1;
            @(posedge ap_clk);
            ap_start <= 0;

            cycles = 0;
            while(!ap_done && cycles < 100000) begin
                @(posedge ap_clk);
                cycles = cycles + 1;
            end
            if(!ap_done) begin
                $display("PROPAGATION_RTL_TIMEOUT scenario=%0d requests=%0d lengths=%0d", selected, request_count, length_count);
                $display("top_fsm=%h length_valid/ready=%b/%b request_valid/ready=%b/%b",
                    dut.ap_CS_fsm, clauseLengths_TVALID,
                    clauseLengths_TREADY, clauseRequests_TVALID,
                    clauseRequests_TREADY);
                $finish;
            end
            repeat(3) @(posedge ap_clk);

            if(captured_height < 0 || captured_backtrack < 0 ||
                    captured_conflicts < 0) begin
                $display("PROPAGATION_RTL_MISSING_OUTPUT scenario=%0d", selected);
                $finish;
            end
            if($isunknown({captured_height, captured_first, captured_second,
                    captured_backtrack, captured_conflicts, request_count,
                    last_request, length_count})) begin
                $display("PROPAGATION_RTL_UNKNOWN_OUTPUT scenario=%0d", selected);
                $finish;
            end
            scenario_matches = 1;
            case(selected)
                0: if(captured_backtrack != 0 || captured_height != 1 ||
                        captured_first != 1 || request_count != 0) scenario_matches = 0;
                1: if(captured_backtrack != 0 || captured_height != 2 ||
                        captured_first != 3 || captured_second != 4 ||
                        request_count != 1 || last_request != 8) scenario_matches = 0;
                2: if(captured_backtrack != 1 || captured_conflicts != 1 ||
                        request_count != 0) scenario_matches = 0;
                3: if(captured_backtrack != 0 || captured_height != 1 ||
                        captured_first != 7 || request_count != 0) scenario_matches = 0;
                4: if(captured_backtrack != 0 || captured_height != 1 ||
                        captured_first != 9 || request_count != 0) scenario_matches = 0;
                5: if(captured_backtrack != 1 || captured_height != 0 ||
                        request_count != 0) scenario_matches = 0;
                6: if(captured_backtrack != 0 || captured_conflicts != 0 ||
                        captured_height != 1 || captured_first != 13 ||
                        request_count != 0) scenario_matches = 0;
                7: if(captured_backtrack != 0 || captured_conflicts != 0 ||
                        captured_height != 1 || captured_first != 15 ||
                        request_count != 0) scenario_matches = 0;
                8: if(captured_backtrack != 0 || captured_conflicts != 0 ||
                        captured_height != 3 || captured_first != 17 ||
                        captured_second != 19 || request_count != 1 ||
                        last_request != 64) scenario_matches = 0;
                9, 10, 11, 12:
                    if(captured_backtrack != 0 || captured_conflicts != 0 ||
                        captured_height != selected - 5 ||
                        captured_first != 1 + 2*selected ||
                        captured_second != 3*selected - 5 ||
                        request_count != 2 ||
                        last_request != 1 + 8*selected) scenario_matches = 0;
            endcase
            if(!scenario_matches) begin
                $display("PROPAGATION_RTL_OUTPUT_MISMATCH scenario=%0d height=%0d first=%0d second=%0d backtrack=%0d conflicts=%0d requests=%0d last=%0d",
                    selected, captured_height, captured_first, captured_second,
                    captured_backtrack, captured_conflicts, request_count,
                    last_request);
                $finish;
            end
            if(clauseLengths_TVALID || length_count != expected_lengths) begin
                $display("PROPAGATION_RTL_LENGTH_MISMATCH scenario=%0d valid=%b consumed=%0d expected=%0d",
                    selected, clauseLengths_TVALID, length_count,
                    expected_lengths);
                $finish;
            end
            $display("PROPAGATION_RTL_SCENARIO_PASS scenario=%0d cycles=%0d", selected, cycles);
        end
    endtask

    initial begin
        integer only_scenario;
        repeat(8) @(posedge ap_clk);
        ap_rst_n <= 1;
        repeat(4) @(posedge ap_clk);
        if($value$plusargs("SCENARIO=%d", only_scenario)) begin
            run_scenario(only_scenario);
            $display("PROPAGATION_CLOSED_LOOP_RTL_PASS");
            $finish;
        end
        run_scenario(0);
        run_scenario(1);
        run_scenario(2);
        run_scenario(3);
        run_scenario(4);
        run_scenario(5);
        run_scenario(6);
        run_scenario(7);
        run_scenario(8);
        run_scenario(9);
        run_scenario(10);
        run_scenario(11);
        run_scenario(12);
        $display("PROPAGATION_CLOSED_LOOP_RTL_PASS");
        $finish;
    end
endmodule
