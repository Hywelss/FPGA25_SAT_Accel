`timescale 1ns/1ps

module clause_length_protocol_rtl_tb;
    logic ap_clk = 0;
    logic ap_rst_n = 0;
    logic ap_start = 0;
    wire ap_done;
    wire ap_idle;
    wire ap_ready;

    wire [2:0] commands_address0;
    wire commands_ce0;
    logic [63:0] commands_q0 = 0;
    logic [63:0] commands [0:7];

    logic [95:0] input_r_TDATA = 0;
    logic input_r_TVALID = 0;
    wire input_r_TREADY;
    wire [31:0] output_r_TDATA;
    wire output_r_TVALID;
    logic output_r_TREADY = 0;

    logic [15:0] lfsr = 16'h51a7;
    integer accepted = 0;
    integer produced = 0;
    integer errors = 0;

    always #2.5 ap_clk = ~ap_clk;

    always_ff @(posedge ap_clk) begin
        if(!ap_rst_n) begin
            lfsr <= 16'h51a7;
            output_r_TREADY <= 0;
        end else begin
            lfsr <= {lfsr[14:0], lfsr[15] ^ lfsr[13] ^ lfsr[12] ^ lfsr[10]};
            output_r_TREADY <= lfsr[0] | lfsr[4];
            if(input_r_TVALID && input_r_TREADY)
                accepted <= accepted + 1;
            if(output_r_TVALID && output_r_TREADY)
                produced <= produced + 1;
            if(commands_ce0)
                commands_q0 <= commands[commands_address0];
        end
    end

    clauseLengthProtocolCosim dut (
        .ap_clk, .ap_rst_n, .ap_start, .ap_done, .ap_idle, .ap_ready,
        .commands_address0, .commands_ce0, .commands_q0,
        .input_r_TDATA, .input_r_TVALID, .input_r_TREADY,
        .input_r_TKEEP(12'hfff), .input_r_TSTRB(12'hfff),
        .input_r_TLAST(1'b0),
        .output_r_TDATA, .output_r_TVALID, .output_r_TREADY,
        .output_r_TKEEP(), .output_r_TSTRB(), .output_r_TLAST()
    );

    task automatic send(input logic [31:0] code, input logic [31:0] value);
        begin
            repeat(lfsr[2:1]) @(posedge ap_clk);
            @(negedge ap_clk);
            input_r_TDATA = {code, 32'd0, value};
            input_r_TVALID = 1;
            do @(posedge ap_clk); while(!input_r_TREADY);
            @(negedge ap_clk);
            input_r_TVALID = 0;
        end
    endtask

    task automatic receive_expected(input logic [31:0] value);
        begin
            do @(posedge ap_clk); while(!(output_r_TVALID && output_r_TREADY));
            if(output_r_TDATA !== value) begin
                $display("length mismatch expected=%0d got=%0d", value,
                    output_r_TDATA);
                errors = errors + 1;
            end
        end
    endtask

    initial begin : timeout_guard
        repeat(100000) @(posedge ap_clk);
        $display("LENGTH_PROTOCOL_TIMEOUT accepted=%0d produced=%0d in=%b/%b out=%b/%b",
            accepted, produced, input_r_TVALID, input_r_TREADY,
            output_r_TVALID, output_r_TREADY);
        $fatal(1);
    end

    initial begin : test
        integer i;
        for(i = 0; i < 8; i = i + 1)
            commands[i] = {32'(i + 2), 32'(i * 4)};

        repeat(8) @(posedge ap_clk);
        ap_rst_n = 1;
        repeat(4) @(posedge ap_clk);
        ap_start = 1;
        @(posedge ap_clk);
        ap_start = 0;

        fork
            begin
                send(32'd2, 32'd0);
                send(32'd0, 32'd1);
                send(32'd0, 32'd0);
                send(32'd2, 32'hffffffff);

                send(32'd2, 32'd0);
                send(32'd2, 32'hffffffff);

                send(32'd2, 32'd0);
                send(32'd0, 32'd2);
                send(32'd2, 32'hffffffff);
                send(32'hffffffff, 32'd0);
            end
            begin
                receive_expected(32'd3);
                receive_expected(32'd2);
                receive_expected(32'd4);
            end
        join

        wait(ap_done);
        repeat(4) @(posedge ap_clk);
        if(accepted != 10 || produced != 3 || errors != 0)
            $fatal(1, "length protocol accounting failed");
        $display("CLAUSE_LENGTH_PROTOCOL_RTL_PASS accepted=%0d produced=%0d",
            accepted, produced);
        $finish;
    end
endmodule
