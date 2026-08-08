`timescale 1ns/1ps

module pq_handler_rtl_closed_loop_tb;
    localparam logic [31:0] PQ_EXIT = 32'hffffffff;
    localparam logic [31:0] PQ_GET_UNDECIDED = 32'd1;
    localparam logic [31:0] PQ_UNHIDE_ELE = 32'd3;
    localparam logic [31:0] PQ_HIDE_ELE = 32'd6;
    localparam logic [63:0] DOMAIN_BASE = 64'h0000_0000_0010_0000;

    logic ap_clk = 0;
    logic ap_rst_n = 0;
    always #2.5 ap_clk = ~ap_clk;

    wire m_axi_gmemDomain_AWVALID;
    logic m_axi_gmemDomain_AWREADY = 1;
    wire [63:0] m_axi_gmemDomain_AWADDR;
    wire m_axi_gmemDomain_WVALID;
    logic m_axi_gmemDomain_WREADY = 1;
    wire [31:0] m_axi_gmemDomain_WDATA;
    wire [3:0] m_axi_gmemDomain_WSTRB;
    wire m_axi_gmemDomain_WLAST;
    wire m_axi_gmemDomain_ARREADY;
    wire m_axi_gmemDomain_ARVALID;
    wire [63:0] m_axi_gmemDomain_ARADDR;
    wire [7:0] m_axi_gmemDomain_ARLEN;
    logic m_axi_gmemDomain_RVALID = 0;
    wire m_axi_gmemDomain_RREADY;
    logic [31:0] m_axi_gmemDomain_RDATA = 0;
    logic m_axi_gmemDomain_RLAST = 0;
    logic m_axi_gmemDomain_BVALID = 0;
    wire m_axi_gmemDomain_BREADY;

    logic [31:0] input_r_TDATA = 0;
    logic input_r_TVALID = 0;
    wire input_r_TREADY;
    wire [31:0] output_r_TDATA;
    wire output_r_TVALID;
    logic output_r_TREADY = 0;

    logic s_axi_control_AWVALID = 0;
    wire s_axi_control_AWREADY;
    logic [5:0] s_axi_control_AWADDR = 0;
    logic s_axi_control_WVALID = 0;
    wire s_axi_control_WREADY;
    logic [31:0] s_axi_control_WDATA = 0;
    logic [3:0] s_axi_control_WSTRB = 4'hf;
    logic s_axi_control_ARVALID = 0;
    wire s_axi_control_ARREADY;
    logic [5:0] s_axi_control_ARADDR = 0;
    wire s_axi_control_RVALID;
    logic s_axi_control_RREADY = 1;
    wire [31:0] s_axi_control_RDATA;
    wire [1:0] s_axi_control_RRESP;
    wire s_axi_control_BVALID;
    logic s_axi_control_BREADY = 1;
    wire [1:0] s_axi_control_BRESP;
    wire interrupt;

    integer errors = 0;
    integer read_index = 0;
    integer read_beats = 0;
    integer expected_variable = 0;
    integer pending_expected = -1;
    integer test_stage = 0;
    integer wait_cycles = 0;

    // This memory model has storage for one read burst. Backpressure later
    // addresses until every beat of the current burst has been accepted.
    assign m_axi_gmemDomain_ARREADY = !m_axi_gmemDomain_RVALID;

    pqHandler dut (
        .ap_clk, .ap_rst_n,
        .m_axi_gmemDomain_AWVALID, .m_axi_gmemDomain_AWREADY,
        .m_axi_gmemDomain_AWADDR, .m_axi_gmemDomain_AWID(),
        .m_axi_gmemDomain_AWLEN(), .m_axi_gmemDomain_AWSIZE(),
        .m_axi_gmemDomain_AWBURST(), .m_axi_gmemDomain_AWLOCK(),
        .m_axi_gmemDomain_AWCACHE(), .m_axi_gmemDomain_AWPROT(),
        .m_axi_gmemDomain_AWQOS(), .m_axi_gmemDomain_AWREGION(),
        .m_axi_gmemDomain_AWUSER(), .m_axi_gmemDomain_WVALID,
        .m_axi_gmemDomain_WREADY, .m_axi_gmemDomain_WDATA,
        .m_axi_gmemDomain_WSTRB, .m_axi_gmemDomain_WLAST,
        .m_axi_gmemDomain_WID(), .m_axi_gmemDomain_WUSER(),
        .m_axi_gmemDomain_ARVALID, .m_axi_gmemDomain_ARREADY,
        .m_axi_gmemDomain_ARADDR, .m_axi_gmemDomain_ARID(),
        .m_axi_gmemDomain_ARLEN, .m_axi_gmemDomain_ARSIZE(),
        .m_axi_gmemDomain_ARBURST(), .m_axi_gmemDomain_ARLOCK(),
        .m_axi_gmemDomain_ARCACHE(), .m_axi_gmemDomain_ARPROT(),
        .m_axi_gmemDomain_ARQOS(), .m_axi_gmemDomain_ARREGION(),
        .m_axi_gmemDomain_ARUSER(), .m_axi_gmemDomain_RVALID,
        .m_axi_gmemDomain_RREADY, .m_axi_gmemDomain_RDATA,
        .m_axi_gmemDomain_RLAST, .m_axi_gmemDomain_RID(1'b0),
        .m_axi_gmemDomain_RUSER(1'b0), .m_axi_gmemDomain_RRESP(2'b00),
        .m_axi_gmemDomain_BVALID, .m_axi_gmemDomain_BREADY,
        .m_axi_gmemDomain_BRESP(2'b00), .m_axi_gmemDomain_BID(1'b0),
        .m_axi_gmemDomain_BUSER(1'b0),
        .input_r_TDATA, .input_r_TVALID, .input_r_TREADY,
        .input_r_TKEEP(4'hf), .input_r_TSTRB(4'hf), .input_r_TLAST(1'b0),
        .output_r_TDATA, .output_r_TVALID, .output_r_TREADY,
        .output_r_TKEEP(), .output_r_TSTRB(), .output_r_TLAST(),
        .s_axi_control_AWVALID, .s_axi_control_AWREADY,
        .s_axi_control_AWADDR, .s_axi_control_WVALID,
        .s_axi_control_WREADY, .s_axi_control_WDATA,
        .s_axi_control_WSTRB, .s_axi_control_ARVALID,
        .s_axi_control_ARREADY, .s_axi_control_ARADDR,
        .s_axi_control_RVALID, .s_axi_control_RREADY,
        .s_axi_control_RDATA, .s_axi_control_RRESP,
        .s_axi_control_BVALID, .s_axi_control_BREADY,
        .s_axi_control_BRESP, .interrupt
    );

    always_ff @(posedge ap_clk) begin
        if(!ap_rst_n) begin
            m_axi_gmemDomain_RVALID <= 0;
            m_axi_gmemDomain_RLAST <= 0;
            read_index <= 0;
            read_beats <= 0;
        end else begin
            if(m_axi_gmemDomain_ARVALID && m_axi_gmemDomain_ARREADY) begin
                read_index <= (m_axi_gmemDomain_ARADDR - DOMAIN_BASE) >> 2;
                read_beats <= m_axi_gmemDomain_ARLEN + 1;
                m_axi_gmemDomain_RVALID <= 1;
                m_axi_gmemDomain_RDATA <= ((m_axi_gmemDomain_ARADDR - DOMAIN_BASE) >> 2) + 1;
                m_axi_gmemDomain_RLAST <= (m_axi_gmemDomain_ARLEN == 0);
            end else if(m_axi_gmemDomain_RVALID && m_axi_gmemDomain_RREADY) begin
                if(read_beats <= 1) begin
                    m_axi_gmemDomain_RVALID <= 0;
                    m_axi_gmemDomain_RLAST <= 0;
                end else begin
                    read_index <= read_index + 1;
                    read_beats <= read_beats - 1;
                    m_axi_gmemDomain_RDATA <= read_index + 2;
                    m_axi_gmemDomain_RLAST <= (read_beats == 2);
                end
            end
        end
    end

    task automatic control_write(input logic [5:0] address, input logic [31:0] value);
        bit aw_done;
        bit w_done;
        begin
            aw_done = 0;
            w_done = 0;
            @(negedge ap_clk);
            s_axi_control_AWADDR = address;
            s_axi_control_AWVALID = 1;
            s_axi_control_WDATA = value;
            s_axi_control_WVALID = 1;
            while(!aw_done || !w_done) begin
                @(posedge ap_clk);
                if(s_axi_control_AWVALID && s_axi_control_AWREADY)
                    aw_done = 1;
                if(s_axi_control_WVALID && s_axi_control_WREADY)
                    w_done = 1;
                @(negedge ap_clk);
                if(aw_done)
                    s_axi_control_AWVALID = 0;
                if(w_done)
                    s_axi_control_WVALID = 0;
            end
            do @(posedge ap_clk); while(!s_axi_control_BVALID);
        end
    endtask

    task automatic wait_for_idle;
        logic [31:0] status;
        begin
            status = 0;
            while(!status[2]) begin
                @(negedge ap_clk);
                s_axi_control_ARADDR = 6'h00;
                s_axi_control_ARVALID = 1;
                do @(posedge ap_clk); while(!s_axi_control_ARREADY);
                @(negedge ap_clk);
                s_axi_control_ARVALID = 0;
                do @(posedge ap_clk); while(!s_axi_control_RVALID);
                status = s_axi_control_RDATA;
            end
        end
    endtask

    task automatic send_command(input logic [31:0] command);
        begin
            @(negedge ap_clk);
            input_r_TDATA = command;
            input_r_TVALID = 1;
            do @(posedge ap_clk); while(!input_r_TREADY);
            @(negedge ap_clk);
            input_r_TVALID = 0;
        end
    endtask

    task automatic receive_value(input logic [31:0] expected, input integer delay_cycles);
        begin
            pending_expected = expected;
            output_r_TREADY = 0;
            repeat(delay_cycles) @(posedge ap_clk);
            @(negedge ap_clk);
            output_r_TREADY = 1;
            do @(posedge ap_clk); while(!output_r_TVALID);
            if(output_r_TDATA !== expected) begin
                $display("value mismatch: expected %0d, got %0d", expected, output_r_TDATA);
                errors = errors + 1;
            end
            @(negedge ap_clk);
            output_r_TREADY = 0;
            pending_expected = -1;
        end
    endtask

    task automatic start_transaction(
        input logic reset_session,
        input logic [31:0] num_literals,
        input logic [31:0] num_domain_literals);
        begin
            control_write(6'h10, DOMAIN_BASE[31:0]);
            control_write(6'h14, DOMAIN_BASE[63:32]);
            control_write(6'h1c, num_literals);
            control_write(6'h24, num_domain_literals);
            control_write(6'h2c, 32'h66666666);
            control_write(6'h30, 32'h3fee6666);
            control_write(6'h38, reset_session);
            control_write(6'h00, 32'd1);
        end
    endtask

    task automatic get_and_expect(input logic [31:0] expected, input integer delay_cycles);
        begin
            send_command(PQ_GET_UNDECIDED);
            receive_value(expected, delay_cycles);
        end
    endtask

    task automatic run_full_domain(input integer stage, input integer count);
        begin
            test_stage = stage;
            start_transaction(1, count, count);
            for(expected_variable = count; expected_variable >= 1;
                    expected_variable = expected_variable - 1)
                get_and_expect(expected_variable, expected_variable % 7);
            get_and_expect(32'd0, 3);
            send_command(PQ_EXIT);
            send_command(PQ_EXIT);
            wait_for_idle;
        end
    endtask

    task automatic hide_one(input logic [31:0] variable);
        begin
            send_command(PQ_HIDE_ELE);
            send_command(variable);
            send_command(PQ_EXIT);
        end
    endtask

    always_ff @(posedge ap_clk) begin : timeout_guard
        if(!ap_rst_n || pending_expected < 0) begin
            wait_cycles <= 0;
        end else if(wait_cycles == 100000) begin
            $display("timeout: stage=%0d expected=%0d in valid/ready=%b/%b out valid/ready=%b/%b out=%0d",
                test_stage, pending_expected,
                input_r_TVALID, input_r_TREADY, output_r_TVALID,
                output_r_TREADY, output_r_TDATA);
            $display("memory: ar valid/ready=%b/%b addr=%h len=%0d r valid/ready/last=%b/%b/%b data=%0d beats=%0d index=%0d",
                m_axi_gmemDomain_ARVALID, m_axi_gmemDomain_ARREADY,
                m_axi_gmemDomain_ARADDR, m_axi_gmemDomain_ARLEN,
                m_axi_gmemDomain_RVALID, m_axi_gmemDomain_RREADY,
                m_axi_gmemDomain_RLAST, m_axi_gmemDomain_RDATA,
                read_beats, read_index);
            $fatal(1, "priority queue closed-loop RTL test timed out");
        end else begin
            wait_cycles <= wait_cycles + 1;
        end
    end

    initial begin : test
        repeat(8) @(posedge ap_clk);
        ap_rst_n = 1;
        repeat(4) @(posedge ap_clk);

        test_stage = 1;
        start_transaction(1, 6, 6);
        send_command(PQ_HIDE_ELE);
        send_command(32'd3);
        send_command(PQ_EXIT);
        get_and_expect(32'd6, 11);
        get_and_expect(32'd5, 3);
        get_and_expect(32'd4, 7);
        get_and_expect(32'd2, 1);
        get_and_expect(32'd1, 13);
        get_and_expect(32'd0, 5);
        send_command(PQ_EXIT);
        send_command(PQ_UNHIDE_ELE);
        send_command(32'd3);
        send_command(PQ_EXIT);
        get_and_expect(32'd3, 9);
        send_command(PQ_EXIT);
        send_command(PQ_EXIT);
        wait_for_idle;

        test_stage = 2;
        start_transaction(0, 6, 6);
        get_and_expect(32'd6, 5);
        get_and_expect(32'd5, 1);
        send_command(PQ_EXIT);
        send_command(PQ_EXIT);
        wait_for_idle;

        test_stage = 3;
        start_transaction(1, 3, 1);
        get_and_expect(32'd1, 5);
        get_and_expect(32'd0, 3);
        send_command(PQ_EXIT);
        send_command(PQ_EXIT);
        wait_for_idle;

        test_stage = 4;
        start_transaction(1, 8, 8);
        hide_one(32'd8);
        get_and_expect(32'd7, 11);
        get_and_expect(32'd6, 3);
        get_and_expect(32'd5, 7);
        get_and_expect(32'd4, 1);
        get_and_expect(32'd3, 13);
        get_and_expect(32'd2, 5);
        get_and_expect(32'd1, 9);
        get_and_expect(32'd0, 3);
        send_command(PQ_EXIT);
        send_command(PQ_EXIT);
        wait_for_idle;

        test_stage = 5;
        start_transaction(1, 8, 8);
        hide_one(32'd8);
        hide_one(32'd3);
        hide_one(32'd5);
        hide_one(32'd6);
        hide_one(32'd7);
        get_and_expect(32'd4, 13);
        get_and_expect(32'd2, 1);
        get_and_expect(32'd1, 9);
        get_and_expect(32'd0, 5);
        send_command(PQ_EXIT);
        send_command(PQ_EXIT);
        wait_for_idle;

        run_full_domain(6, 8);
        run_full_domain(7, 16);
        run_full_domain(8, 32);
        run_full_domain(9, 64);

        test_stage = 10;
        start_transaction(1, 63, 63);
        for(expected_variable = 63; expected_variable >= 1;
                expected_variable = expected_variable - 1)
            get_and_expect(expected_variable, expected_variable % 5);
        get_and_expect(32'd0, 3);
        send_command(PQ_EXIT);
        send_command(PQ_EXIT);
        wait_for_idle;

        run_full_domain(11, 216);
        run_full_domain(12, 729);
        run_full_domain(13, 1024);
        run_full_domain(14, 2810);

        if(errors != 0)
            $fatal(1, "priority queue closed-loop RTL test failed with %0d mismatches", errors);
        $display("PQ_CLOSED_LOOP_RTL_PASS");
        $finish;
    end
endmodule
