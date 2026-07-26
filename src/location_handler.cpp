#include <hls_stream.h>
#include <ap_axi_sdata.h>
#include <ap_utils.h>
#include "data_structures.h"


extern "C"{
void location_handler(ap_uint<32>* litToClsStorePos, ap_uint<32>* clsToLitStorePos,
    hls::stream<ap_axiu<64,0,0,0>>& locationInputStream, hls::stream<ap_axiu<32,0,0,0>>& locationOutputStream){

    #pragma HLS INTERFACE m_axi port=litToClsStorePos offset=slave bundle=gmemLoc latency=64 num_write_outstanding=16 num_read_outstanding=16
    #pragma HLS INTERFACE m_axi port=clsToLitStorePos offset=slave bundle=gmemLoc2 latency=64 num_write_outstanding=16 num_read_outstanding=16
    #pragma HLS INTERFACE s_axilite port=litToClsStorePos
    #pragma HLS INTERFACE s_axilite port=clsToLitStorePos
	#pragma HLS INTERFACE axis port=locationInputStream
    #pragma HLS INTERFACE axis port=locationOutputStream
	#pragma HLS INTERFACE s_axilite port=return

    // The companion of the map below, with the same access profile: both are
    // read and written only while clauses are being saved or deleted. Holding
    // this one on chip cost ~57 URAMs to serve garbage collection, which is the
    // rarest thing the solver does, so it belongs in DDR beside the other. Flat
    // 32-bit entries for the same reason: the 128-bit packing suited URAM and in
    // DDR only forced a read-modify-write to preserve neighbouring lanes.
    ap_uint<32>* mClsToLitStorePos = clsToLitStorePos;

    // Occurrence-address-indexed back-reference map. An on-chip array would cost
    // ~57 URAMs and, worse, would have to grow in lockstep with the occurrence
    // address space, which is what actually caps occurrence capacity. Keeping it
    // in DDR removes that cap and frees the URAM for the hot-page cache.
    //
    // Indexed one entry per occurrence element rather than four packed into a
    // 128-bit word. The packing existed to suit URAM; in DDR it forced a
    // read-modify-write on every update, purely to preserve the three
    // neighbouring lanes, which doubled the traffic on the save path and made
    // each update carry a read-after-write dependency. Flat 32-bit entries make
    // a save a single write.
    ap_uint<32>* mLitToClsStorePos = litToClsStorePos;

    LOCATION_HANDLE_LOOP: while(true){
        #pragma HLS loop_tripcount min=16 max=16
        ap_axiu<64,0,0,0> getCommand = locationInputStream.read();
        unsigned int code = getCommand.data.range(31,0);

        if(code == lh::EXIT){
            break;
        }else if(code == lh::SEND){
            SEND_LIT_STORE_LOCATION: while(true){
                #pragma HLS loop_tripcount min=16 max=16
                ap_axiu<64,0,0,0> value = locationInputStream.read();

                if(value.data == lh::EXIT){
                    break;
                }
                const unsigned int addr = value.data.range(31,0);
                ap_axiu<32,0,0,0> send;
                send.data = mClsToLitStorePos[addr];
                locationOutputStream.write(send);
            }
        }else if(code == lh::SAVE){
            SET_LIT_AND_CLS_STORE_LOCATION: while(true){
                #pragma HLS loop_tripcount min=16 max=16
                // No independence assertion: this loop read-modify-writes
                // mLitToClsStorePos, which now lives in DDR, so overlapping
                // iterations could issue a read before the previous write has
                // landed. (The assertion that used to sit here was misspelled
                // and therefore ignored; spelling it correctly would have been
                // a latent corruption bug.)
                ap_axiu<64,0,0,0> value = locationInputStream.read();

                if(value.data == lh::EXIT){
                    break;
                }

                const unsigned int clsToLitAddr = value.data.range(31,0);
                mClsToLitStorePos[clsToLitAddr] = value.data.range(63,32);

                const unsigned int litToClsAddr = value.data.range(63,32);
                mLitToClsStorePos[litToClsAddr] = value.data.range(31,0);
            }
        }else if(code == lh::UPDATE){
            UPDATE_LIT_AND_CLS_STORE_LOCATION: while(true){
                #pragma HLS loop_tripcount min=16 max=16
                // Same reasoning as the save loop: mLitToClsStorePos is in DDR
                // and this loop reads two entries and writes one, so successive
                // iterations genuinely can alias.

                ap_axiu<64,0,0,0> value = locationInputStream.read();

                if(value.data == lh::EXIT){
                    break;
                }

                unsigned int swapAddr = value.data.range(31,0);
                unsigned int replaceAddr = value.data.range(63,32);

                // One read and one write. The old packed layout also had to read
                // the destination word first, only to keep its other three lanes
                // intact.
                const unsigned int litToClsAddr = mLitToClsStorePos[swapAddr];
                mLitToClsStorePos[replaceAddr] = litToClsAddr;

                mClsToLitStorePos[litToClsAddr] = replaceAddr;
            }
        }
    }

}
}
