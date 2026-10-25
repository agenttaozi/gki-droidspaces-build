// Copyright ESWIN 2024
/* enable_flags: BIT0 mct singal
 *               BIT1 sct singal
 *               BIT2 mct delta
 *               BIT3 sct delta
 */

#include "eswin_eph861x_types.h"
#include "eswin_eph861x_comms.h"
#include "eswin_eph861x_eswin.h"
#include "eswin_eph861x_bootloader.h"
#include "eswin_eph861x_tlv_report.h"
#include "eswin_eph861x.h"
#include "eswin_eph861x_testing.h"
#include "eswin_eph861x_tlv_command.h"

int16_t pt_max_ref_data[] = {-618.0,456.0,578.0,559.0,584.0,566.0,554.0,580.0,663.0,695.0,634.0,663.0,672.0,686.0,717.0,640.0,593.0,-617.0,-65.0,-76.0,-7.0,-46.0,-43.0,-86.0,-86.0,-74.0,-19.0,-4.0,-53.0,-64.0,-33.0,-27.0,-58.0,-53.0,-77.0,-107.0,-76.0,-137.0,-133.0,-133.0,-117.0,-125.0,-144.0,-134.0,-83.0,-85.0,-140.0,-155.0,-128.0,-117.0,-119.0,-135.0,-114.0,-116.0,-83.0,-196.0,-182.0,-196.0,-187.0,-178.0,-175.0,-175.0,-118.0,-150.0,-189.0,-219.0,-191.0,-192.0,-180.0,-191.0,-212.0,-183.0,-123.0,-223.0,-222.0,-233.0,-222.0,-220.0,-206.0,-190.0,-146.0,-178.0,-246.0,-256.0,-227.0,-227.0,-241.0,-229.0,-247.0,-244.0,-128.0,-234.0,-244.0,-240.0,-251.0,-264.0,-252.0,-213.0,-155.0,-179.0,-258.0,-273.0,-246.0,-249.0,-235.0,-213.0,-219.0,-218.0,-79.0,-182.0,-199.0,-210.0,-222.0,-246.0,-222.0,-209.0,-137.0,-141.0,-217.0,-267.0,-221.0,-227.0,-232.0,-194.0,-186.0,-168.0,-59.0,-141.0,-133.0,-153.0,-149.0,-179.0,-185.0,-199.0,-96.0,-98.0,-154.0,-203.0,-170.0,-160.0,-189.0,-162.0,-154.0,-117.0,-49.0,-94.0,-95.0,-156.0,-157.0,-172.0,-190.0,-190.0,-92.0,-79.0,-162.0,-193.0,-173.0,-167.0,-201.0,-181.0,-203.0,-187.0,-97.0,-161.0,-165.0,-178.0,-173.0,-174.0,-162.0,-173.0,-93.0,-68.0,-145.0,-161.0,-146.0,-161.0,-201.0,-188.0,-219.0,-244.0,-91.0,-166.0,-174.0,-180.0,-178.0,-174.0,-138.0,-150.0,-93.0,-69.0,-122.0,-123.0,-123.0,-146.0,-176.0,-191.0,-217.0,-201.0,-58.0,-130.0,-141.0,-154.0,-142.0,-143.0,-134.0,-148.0,-71.0,-46.0,-107.0,-111.0,-115.0,-125.0,-157.0,-164.0,-169.0,-124.0,-79.0,-127.0,-150.0,-170.0,-152.0,-140.0,-153.0,-152.0,-65.0,-67.0,-150.0,-167.0,-160.0,-147.0,-167.0,-152.0,-148.0,-126.0,-28.0,-90.0,-105.0,-112.0,-99.0,-103.0,-105.0,-114.0,-28.0,-24.0,-111.0,-123.0,-127.0,-104.0,-103.0,-82.0,-71.0,-67.0,0.0,-46.0,-48.0,-47.0,-50.0,-71.0,-71.0,-81.0,12.0,9.0,-77.0,-103.0,-97.0,-77.0,-51.0,-29.0,-28.0,-29.0,-17.0,-64.0,-70.0,-91.0,-91.0,-96.0,-107.0,-117.0,-21.0,-25.0,-106.0,-125.0,-120.0,-107.0,-84.0,-58.0,-62.0,-59.0,-30.0,-104.0,-103.0,-111.0,-124.0,-130.0,-133.0,-140.0,-49.0,-52.0,-113.0,-140.0,-138.0,-135.0,-132.0,-103.0,-107.0,-85.0,-16.0,-67.0,-64.0,-60.0,-64.0,-67.0,-73.0,-70.0,29.0,42.0,-37.0,-43.0,-51.0,-59.0,-72.0,-58.0,-60.0,-54.0,12.0,-16.0,-3.0,-1.0,-8.0,-3.0,-4.0,12.0,101.0,100.0,17.0,11.0,19.0,11.0,-4.0,9.0,-2.0,-22.0,74.0,24.0,16.0,28.0,14.0,16.0,12.0,21.0,95.0,128.0,48.0,48.0,44.0,40.0,34.0,30.0,31.0,131.0,98.0,40.0,30.0,39.0,46.0,22.0,18.0,7.0,92.0,128.0,57.0,61.0,61.0,67.0,61.0,36.0,44.0,118.0,122.0,54.0,60.0,67.0,75.0,61.0,40.0,34.0,139.0,182.0,110.0,117.0,99.0,107.0,103.0,76.0,83.0,163.0,120.0,54.0,62.0,84.0,85.0,77.0,76.0,77.0,169.0,188.0,100.0,94.0,101.0,116.0,116.0,95.0,82.0,203.0,130.0,75.0,66.0,87.0,93.0,105.0,100.0,147.0,186.0,186.0,112.0,109.0,95.0,142.0,128.0,105.0,117.0,233.0,176.0,124.0,113.0,124.0,145.0,151.0,153.0,145.0,224.0,232.0,161.0,164.0,157.0,191.0,187.0,163.0,185.0,268.0,315.0,267.0,249.0,265.0,267.0,290.0,287.0,261.0,349.0,369.0,293.0,317.0,299.0,322.0,328.0,310.0,346.0,425.0,336.0,267.0,269.0,292.0,311.0,305.0,306.0,294.0,380.0,389.0,303.0,335.0,315.0,350.0,361.0,340.0,384.0,464.0,374.0,275.0,263.0,279.0,293.0,303.0,292.0,294.0,367.0,375.0,292.0,327.0,305.0,334.0,357.0,347.0,400.0,482.0,362.0,286.0,275.0,288.0,292.0,304.0,309.0,292.0,384.0,383.0,288.0,336.0,323.0,328.0,349.0,347.0,393.0,497.0,377.0,312.0,287.0,306.0,316.0,336.0,338.0,308.0,406.0,418.0,300.0,367.0,347.0,350.0,347.0,336.0,391.0,499.0,384.0,317.0,305.0,312.0,320.0,348.0,349.0,330.0,397.0,417.0,311.0,342.0,342.0,356.0,352.0,336.0,381.0,489.0,424.0,371.0,355.0,376.0,365.0,391.0,399.0,401.0,453.0,463.0,371.0,403.0,407.0,404.0,413.0,387.0,424.0,514.0,440.0,378.0,363.0,386.0,397.0,412.0,423.0,394.0,480.0,498.0,387.0,437.0,435.0,422.0,431.0,402.0,413.0,512.0,476.0,426.0,419.0,455.0,449.0,491.0,485.0,439.0,552.0,562.0,452.0,506.0,502.0,490.0,494.0,447.0,452.0,584.0,483.0,434.0,441.0,478.0,484.0,513.0,507.0,463.0,551.0,593.0,495.0,529.0,512.0,506.0,512.0,468.0,494.0,613.0,502.0,469.0,459.0,493.0,497.0,517.0,516.0,487.0,594.0,609.0,490.0,534.0,515.0,526.0,537.0,492.0,515.0,651.0,511.0,493.0,477.0,518.0,527.0,551.0,568.0,561.0,710.0,712.0,546.0,571.0,539.0,554.0,575.0,536.0,568.0,692.0,630.0,616.0,628.0,666.0,659.0,698.0,712.0,729.0,-258.0,-334.0,705.0,737.0,694.0,689.0,720.0,678.0,706.0,807.0,-281.0,817.0,937.0,989.0,998.0,1021.0,1066.0,1074.0,-640.0,-628.0,1078.0,1137.0,1072.0,1097.0,1106.0,1102.0,1078.0,104.0};
int16_t pt_min_ref_data[] = {-969.0,61.0,133.0,75.0,65.0,25.0,15.0,46.0,94.0,63.0,54.0,25.0,49.0,2.0,15.0,16.0,-98.0,-1198.0,-450.0,-470.0,-443.0,-500.0,-514.0,-572.0,-582.0,-565.0,-543.0,-560.0,-579.0,-627.0,-598.0,-629.0,-644.0,-604.0,-667.0,-676.0,-441.0,-509.0,-527.0,-558.0,-559.0,-582.0,-613.0,-594.0,-577.0,-609.0,-634.0,-680.0,-652.0,-676.0,-673.0,-649.0,-672.0,-647.0,-437.0,-547.0,-552.0,-597.0,-598.0,-607.0,-617.0,-608.0,-581.0,-632.0,-658.0,-701.0,-679.0,-705.0,-688.0,-668.0,-708.0,-671.0,-443.0,-549.0,-559.0,-599.0,-596.0,-610.0,-609.0,-589.0,-566.0,-614.0,-662.0,-688.0,-667.0,-687.0,-683.0,-660.0,-688.0,-671.0,-434.0,-545.0,-560.0,-587.0,-599.0,-629.0,-630.0,-585.0,-551.0,-587.0,-646.0,-675.0,-655.0,-675.0,-658.0,-620.0,-638.0,-621.0,-380.0,-489.0,-509.0,-548.0,-561.0,-597.0,-585.0,-563.0,-515.0,-531.0,-589.0,-650.0,-610.0,-630.0,-630.0,-579.0,-589.0,-559.0,-337.0,-423.0,-425.0,-466.0,-467.0,-504.0,-515.0,-515.0,-438.0,-458.0,-495.0,-562.0,-527.0,-533.0,-549.0,-509.0,-518.0,-476.0,-341.0,-392.0,-402.0,-451.0,-456.0,-475.0,-499.0,-484.0,-414.0,-413.0,-478.0,-522.0,-499.0,-511.0,-531.0,-499.0,-527.0,-508.0,-346.0,-412.0,-423.0,-440.0,-441.0,-447.0,-446.0,-443.0,-386.0,-370.0,-436.0,-460.0,-442.0,-463.0,-498.0,-472.0,-511.0,-524.0,-323.0,-398.0,-411.0,-423.0,-427.0,-426.0,-396.0,-398.0,-357.0,-348.0,-389.0,-402.0,-398.0,-423.0,-451.0,-450.0,-482.0,-458.0,-264.0,-338.0,-347.0,-363.0,-361.0,-366.0,-354.0,-353.0,-294.0,-285.0,-332.0,-345.0,-341.0,-355.0,-391.0,-384.0,-391.0,-352.0,-241.0,-291.0,-308.0,-328.0,-320.0,-314.0,-320.0,-311.0,-237.0,-251.0,-316.0,-338.0,-327.0,-323.0,-344.0,-323.0,-323.0,-300.0,-162.0,-223.0,-230.0,-239.0,-239.0,-241.0,-239.0,-237.0,-164.0,-174.0,-241.0,-262.0,-259.0,-243.0,-247.0,-219.0,-217.0,-210.0,-109.0,-151.0,-147.0,-148.0,-156.0,-173.0,-172.0,-170.0,-91.0,-102.0,-173.0,-201.0,-191.0,-175.0,-160.0,-137.0,-139.0,-141.0,-96.0,-150.0,-143.0,-162.0,-164.0,-165.0,-173.0,-175.0,-91.0,-99.0,-167.0,-187.0,-183.0,-165.0,-154.0,-132.0,-133.0,-127.0,-95.0,-169.0,-162.0,-166.0,-175.0,-177.0,-183.0,-181.0,-99.0,-101.0,-162.0,-185.0,-185.0,-177.0,-175.0,-154.0,-152.0,-126.0,-111.0,-115.0,-111.0,-105.0,-110.0,-114.0,-116.0,-120.0,-14.0,-4.0,-89.0,-103.0,-106.0,-126.0,-128.0,-104.0,-103.0,-139.0,-85.0,-54.0,-49.0,-51.0,-73.0,-70.0,-67.0,-55.0,40.0,37.0,-58.0,-71.0,-57.0,-72.0,-77.0,-54.0,-60.0,-121.0,0.0,-42.0,-59.0,-58.0,-82.0,-88.0,-87.0,-84.0,-2.0,26.0,-60.0,-70.0,-70.0,-87.0,-91.0,-83.0,-90.0,62.0,-15.0,-48.0,-69.0,-70.0,-74.0,-107.0,-105.0,-125.0,-32.0,-6.0,-75.0,-88.0,-78.0,-92.0,-93.0,-103.0,-106.0,-44.0,-4.0,-60.0,-61.0,-60.0,-69.0,-92.0,-108.0,-119.0,-5.0,26.0,-43.0,-55.0,-64.0,-76.0,-76.0,-84.0,-91.0,-29.0,-26.0,-78.0,-78.0,-62.0,-78.0,-96.0,-89.0,-90.0,10.0,12.0,-75.0,-103.0,-83.0,-89.0,-84.0,-82.0,-117.0,-11.0,-27.0,-63.0,-93.0,-79.0,-88.0,-75.0,-84.0,-5.0,12.0,-5.0,-85.0,-103.0,-116.0,-83.0,-87.0,-97.0,-102.0,9.0,-1.0,-44.0,-65.0,-64.0,-51.0,-50.0,-51.0,-61.0,24.0,24.0,-57.0,-65.0,-71.0,-49.0,-47.0,-59.0,-52.0,21.0,115.0,82.0,48.0,53.0,49.0,65.0,59.0,30.0,120.0,140.0,56.0,64.0,51.0,62.0,74.0,66.0,92.0,164.0,123.0,63.0,57.0,67.0,78.0,64.0,63.0,52.0,145.0,146.0,52.0,70.0,54.0,78.0,93.0,87.0,119.0,194.0,155.0,60.0,41.0,41.0,48.0,46.0,34.0,40.0,116.0,114.0,26.0,47.0,24.0,43.0,69.0,81.0,116.0,194.0,119.0,50.0,35.0,32.0,24.0,26.0,34.0,15.0,114.0,103.0,-2.0,33.0,27.0,14.0,36.0,59.0,86.0,187.0,123.0,63.0,31.0,34.0,35.0,42.0,51.0,19.0,120.0,121.0,-8.0,47.0,32.0,22.0,16.0,28.0,67.0,169.0,129.0,65.0,45.0,36.0,35.0,49.0,53.0,42.0,108.0,112.0,4.0,10.0,24.0,20.0,11.0,19.0,45.0,149.0,152.0,107.0,86.0,88.0,73.0,84.0,92.0,114.0,154.0,151.0,55.0,71.0,77.0,60.0,68.0,61.0,81.0,167.0,173.0,121.0,101.0,108.0,113.0,114.0,129.0,109.0,190.0,198.0,81.0,117.0,117.0,90.0,94.0,88.0,69.0,169.0,196.0,153.0,143.0,167.0,155.0,188.0,174.0,140.0,251.0,259.0,137.0,180.0,176.0,152.0,153.0,121.0,99.0,237.0,192.0,149.0,155.0,183.0,183.0,204.0,190.0,154.0,243.0,282.0,180.0,199.0,177.0,160.0,162.0,131.0,139.0,264.0,218.0,188.0,180.0,202.0,204.0,219.0,203.0,188.0,301.0,314.0,187.0,210.0,189.0,193.0,203.0,164.0,172.0,325.0,227.0,207.0,197.0,230.0,237.0,260.0,260.0,272.0,438.0,441.0,246.0,252.0,214.0,227.0,248.0,210.0,232.0,377.0,357.0,314.0,326.0,371.0,363.0,403.0,403.0,455.0,-703.0,-836.0,381.0,420.0,360.0,349.0,373.0,338.0,357.0,497.0,-585.0,505.0,649.0,720.0,738.0,774.0,804.0,856.0,-1110.0,-1161.0,773.0,875.0,794.0,817.0,839.0,835.0,826.0,-143.0};

PT_SelftestData selftestdata = NULL;
extern char eswin_touch_criteria_name[];

static int eph_write(struct eph_data *ephdata, u8 write_type, u16 comp_id,
        u16 offset, u8 *data, u32 data_len)
{
    int ret_val = 0;

    u8 *tlv = NULL;
    u16 payload_len = 0;
    u32 tlv_len = 0;

    if (!ephdata) {
        eswin_log(ERR_LOG, "wrong param");
        return -1;
    }
    if ((write_type != TLV_CONFIG_DATA_WRITE) &&
        (write_type != TLV_CONTROL_DATA_WRITE)) {
        eswin_log(ERR_LOG, "wrong param");
        return -1;
    }

    payload_len = sizeof(comp_id) + sizeof(offset) + data_len;
    tlv_len = sizeof(write_type) + sizeof(payload_len) + payload_len;

    tlv = devm_kzalloc(&ephdata->commsdevice->dev, tlv_len, GFP_KERNEL);
    if (!tlv) {
        eswin_log(ERR_LOG, "malloc mem fail");
        return -ENOMEM;
    }

    tlv[0] = write_type;
    tlv[1] = payload_len & 0xFF;
    tlv[2] = (payload_len >> 8) & 0xFF;
    tlv[3] = comp_id & 0xFF;
    tlv[4] = (comp_id >> 8) & 0xFF;
    tlv[5] = offset & 0xFF;
    tlv[6] = (offset >> 8) & 0xFF;
    if (data_len > 0)
        memcpy(&tlv[7], data, data_len);

    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_write_control_config(ephdata, tlv_len, tlv);
    mutex_unlock(&ephdata->comms_mutex);

    return ret_val;
}

static int eph_testing_preprocess(struct eph_data *ephdata, unsigned int enable_flags)
{
    int ret_val = 0;

    u8 type = 0;
    u16 comp_id = 0;
    u16 offset = 0;
    u8 enable = 0;
    unsigned int debug_post_flags = 0;
    //u8 mct_freq_ctrl = 0;

    eswin_log(INFO_LOG, "enter");

    disable_irq(ephdata->chg_irq);

    // tic into active state
    type = TLV_CONTROL_DATA_WRITE;
    comp_id = 0; //control
    offset = 0x16; // param 'force active'
    enable = 1; // disable force active
    ret_val = eph_write(ephdata, type, comp_id, offset,
            &enable, sizeof(enable));
    if (ret_val) {
        eswin_log(ERR_LOG, "eph write fail %x", ret_val);
    }

    // disable report event
    type = TLV_CONFIG_DATA_WRITE;
    comp_id = 500; //comp reporting
    offset = 0; // param 'enable_event_report'
    enable = 0; // disable event report
    ret_val = eph_write(ephdata, type, comp_id, offset,
            &enable, sizeof(enable));
    if (ret_val) {
        eswin_log(ERR_LOG, "eph write fail %x", ret_val);
        goto error;
    }

#if 0 // this make returned wrong eng data
    // fixed freq at index 0
    type = TLV_CONTROL_DATA_WRITE;
    comp_id = 270; // comp sampling
    offset = 12; // param 'host_mct_freq_control'
    /* BIT0 - BIT3: freq index (0, 1, 2, 3)
     * BIT7: enable_manual
     */
    mct_freq_ctrl = BIT(7);
    ret_val = eph_write(ephdata, type, comp_id, offset,
            &mct_freq_ctrl, sizeof(mct_freq_ctrl));
    if (ret_val) {
        eswin_log(ERR_LOG, "eph write fail %x", ret_val);
        goto error;
    }
#endif

    // enable singals
    if (enable_flags & 0x3) {
        type = TLV_CONTROL_DATA_WRITE;
        comp_id = 140; // comp sensing
        offset = 24; // param 'debug_post_flags'
        /* BIT0: mct singals enable
         * BIT1: sct singals enable
         */
        debug_post_flags = (0x1 & enable_flags) | (0x2 & enable_flags);
        ret_val = eph_write(ephdata, type, comp_id, offset,
                (u8 *)&debug_post_flags, sizeof(debug_post_flags));
        if (ret_val) {
            eswin_log(ERR_LOG, "eph write fail %x", ret_val);
            goto error;
        }
    }

    // enable deltas
    if (enable_flags & 0xC) {
        type = TLV_CONTROL_DATA_WRITE;
        comp_id = 270; // comp preprocessing
        offset = 13; // param 'debug_post_flags'
        /* BIT0: mct delta enable
         * BIT1: sct delta enable
         */
        debug_post_flags = (0x1 & (enable_flags >> 2)) | (0x2 & (enable_flags >> 2));
        ret_val = eph_write(ephdata, type, comp_id, offset,
                (u8 *)&debug_post_flags, sizeof(debug_post_flags));
        if (ret_val) {
            eswin_log(ERR_LOG, "eph write fail %x", ret_val);
            goto error;
        }
    }

    return ret_val;

error:
    enable_irq(ephdata->chg_irq);
    return ret_val;
}

/* disable_flags: BIT0 mct singal
 *               BIT1 sct singal
 *               BIT2 mct delta
 *               BIT3 sct delta
 */
static int eph_testing_postprocess(struct eph_data *ephdata, unsigned int disable_flags)
{
    int ret_val = 0;

    u8 type = TLV_CONFIG_DATA_WRITE;
    u16 comp_id = 0;
    u16 offset = 0;
    u8 enable = 0;
    unsigned int debug_post_flags = 0;
    //u8 mct_freq_ctrl = 0;

    eswin_log(INFO_LOG, "enter");

    // disable singals
    if (disable_flags & 0x3) {
        type = TLV_CONTROL_DATA_WRITE;
        comp_id = 140; // comp sensing
        offset = 24; // param 'debug_post_flags'
        /* BIT0: mct singals enable
         * BIT1: sct singals enable
         */
        debug_post_flags = 0;
        ret_val = eph_write(ephdata, type, comp_id, offset,
                (u8 *)&debug_post_flags, sizeof(debug_post_flags));
        if (ret_val) {
            eswin_log(ERR_LOG, "eph write fail %x", ret_val);
        }
    }

    // disable deltas
    if (disable_flags & 0xC) {
        type = TLV_CONTROL_DATA_WRITE;
        comp_id = 270; // comp preprocessing
        offset = 13; // param 'debug_post_flags'
        /* BIT0: mct delta enable
         * BIT1: sct delta enable
         */
        debug_post_flags = 0;
        ret_val = eph_write(ephdata, type, comp_id, offset,
                (u8 *)&debug_post_flags, sizeof(debug_post_flags));
        if (ret_val) {
            eswin_log(ERR_LOG, "eph write fail %x", ret_val);
        }
    }

#if 0
    // restore freq
    type = TLV_CONTROL_DATA_WRITE;
    comp_id = 270; // comp sampling
    offset = 12; // param 'host_mct_freq_control'
    /* BIT0 - BIT3: freq index (0, 1, 2, 3)
     * BIT7: enable_manual
     */
    mct_freq_ctrl = 0;
    ret_val = eph_write(ephdata, type, comp_id, offset,
            &mct_freq_ctrl, sizeof(mct_freq_ctrl));
    if (ret_val) {
        eswin_log(ERR_LOG, "eph write fail %x", ret_val);
    }
#endif

    // enable report data
    type = TLV_CONFIG_DATA_WRITE;
    comp_id = 500; //comp reporting
    offset = 0; // param 'enable_event_report'
    enable = 3; // disable event report
    ret_val = eph_write(ephdata, type, comp_id, offset,
            &enable, sizeof(enable));
    if (ret_val) {
        eswin_log(ERR_LOG, "eph write fail %x", ret_val);
    }

    // tic into normal state
    type = TLV_CONTROL_DATA_WRITE;
    comp_id = 0; //control
    offset = 0x16; // param 'force active'
    enable = 0; // disable force active
    ret_val = eph_write(ephdata, type, comp_id, offset,
            &enable, sizeof(enable));
    if (ret_val) {
        eswin_log(ERR_LOG, "eph write fail %x", ret_val);
    }

    enable_irq(ephdata->chg_irq);

    return ret_val;
}

/* Started by AICoder, pid:u7e8f4348aad8d1148de098cb005233471f4ca52 */
static void eph_save_tp_test_data(int16_t *data_ptr, int type) {
    int i;
    char ch_data[6];

    switch (type) {
        case 1:
            tpd_copy_to_tp_firmware_data("\n==================open and short==================:\n");
            eswin_log(INFO_LOG, "==================open and short data==================");
            break;
        case 2:
            tpd_copy_to_tp_firmware_data("\n==================rawdata==================:\n");
            eswin_log(INFO_LOG, "==================rawdata data==================");
            break;
        case 3:
            tpd_copy_to_tp_firmware_data("\n==================noise part1==================:\n");
            eswin_log(INFO_LOG, "==================noise part1 data==================");
            break;
        case 4:
            tpd_copy_to_tp_firmware_data("\n==================noise part2==================:\n");
            eswin_log(INFO_LOG, "==================noise part2 data==================");
            break;
    }

    for (i = 0; i < TX_NUM_MAX * RX_NUM_MAX; i++) {
        eswin_log(INFO_LOG, "(node, value):(%d, %5d)", (i + 1), data_ptr[i]);
        snprintf(ch_data, sizeof(ch_data), "%5d", data_ptr[i]);
        tpd_copy_to_tp_firmware_data(ch_data);
        memset(ch_data, 0, sizeof(ch_data));
        if ((i + 1) % TX_NUM_MAX == 0) {
            tpd_copy_to_tp_firmware_data("\n");
        }
    }
    tpd_copy_to_tp_firmware_data("\n");
}
/* Ended by AICoder, pid:u7e8f4348aad8d1148de098cb005233471f4ca52 */

/* check open and short */
static int eph_testing_pt01_process(struct eph_data *ephdata, u8 *msg)
{
    int ret_val = 0;
    int i = 0;
    u8 type = 0;
    u16 length = 0;
    u16 comp_id = 0;
    u8 data_id = 0;
    int16_t *data_ptr = NULL;
    int16_t adjust_range = ephdata->pt_threshold_add;

    if (!msg)
        return -1;

    type = msg[0];
    length = *(u16 *)&msg[1];
    comp_id = *(u16 *)&msg[3];
    data_id = msg[5];

    eswin_log(INFO_LOG, "t[%d] len[%d] comp_id[%d] data_id[%d]", type, length, comp_id, data_id);

    if (length <= 0) {
        eswin_log(ERR_LOG, "Not enough len");
        return -1;
    }

    data_ptr = (int16_t *)&msg[7];

    if (sizeof(pt_max_ref_data) > (length - 4)) {
        eswin_log(ERR_LOG, "Received data size not match the ref data");
        return -1;
    }

    for (i = 0; i < sizeof(pt_max_ref_data)/2; i++) {
        if (data_ptr[i] > (pt_max_ref_data[i] + adjust_range)) {
            ret_val = -1;
            break;
        }
    }
    if (i < sizeof(pt_max_ref_data)/2)
        eswin_log(INFO_LOG, "max n[%d] %d %d", i, data_ptr[i], pt_max_ref_data[i]);

    for (i = 0; i < sizeof(pt_min_ref_data)/2; i++) {
        if (data_ptr[i] < (pt_min_ref_data[i] - adjust_range)) {
            ret_val = -1;
            break;
        }
    }
    if (i < sizeof(pt_max_ref_data)/2)
        eswin_log(INFO_LOG, "min n[%d] %d %d", i, data_ptr[i], pt_min_ref_data[i]);

    eph_save_tp_test_data(data_ptr, 1);

    return ret_val;
}

/* check rawdata */
static int eph_testing_pt02_process(struct eph_data *ephdata, u8 *msg)
{
    int ret_val = 0;
    int i = 0, j = 0;
    u8 type = 0;
    u16 length = 0;
    u16 comp_id = 0;
    u8 data_id = 0;
    u16 tx_num = TX_NUM_MAX;
    u16 rx_num = RX_NUM_MAX;
    int16_t *data_ptr = NULL;
    int16_t max_diff = ephdata->pt_flatness_max;
    int16_t diff = 0;

    if (!msg)
        return -1;

    type = msg[0];
    length = *(u16 *)&msg[1];
    comp_id = *(u16 *)&msg[3];
    data_id = msg[5];

    eswin_log(INFO_LOG, "t[%d] len[%d] comp_id[%d] data_id[%d]", type, length, comp_id, data_id);

    data_ptr = (int16_t *)&msg[7];

    /* PT spec need to ignore the top two rows,
     * the bottom two rows and the first cols,
     * the end cols
    */
    /* col check */
    for (i = 1; i < (tx_num-1); i++) {
        for (j = 2; j < (rx_num-2); j++) {
            diff = data_ptr[i * tx_num + j] - data_ptr[i * tx_num + j - 1];
            if (diff < 0)
                diff = -diff;
            if (diff > max_diff) {
                ret_val = -1;
                goto out;
            }
        }
    }

    /* row check */
    for (i = 2; i < (rx_num-2); i++) {
        for (j = 2; j < (tx_num-1); j++) {
            diff = data_ptr[i * rx_num + j] - data_ptr[i * rx_num + j - 1];
            if (diff < 0)
                diff = -diff;
            if (diff > max_diff) {
                ret_val = -1;
                goto out;
            }
        }
    }

out:
    eph_save_tp_test_data(data_ptr, 2);
    return ret_val;
}

/* check max delta (noise part1) */
static int eph_testing_pt03_process(struct eph_data *ephdata, u8 *msg)
{
    int ret_val = 0;
    int i = 0;
    u8 type = 0;
    u16 length = 0;
    u16 comp_id = 0;
    u8 data_id = 0;
    int16_t *data_ptr = NULL;
    int16_t delta = 0;
    int16_t max_delta = ephdata->pt_max_delta;

    if (!msg)
        return -1;

    type = msg[0];
    length = *(u16 *)&msg[1];
    comp_id = *(u16 *)&msg[3];
    data_id = msg[5];

    eswin_log(INFO_LOG, "t[%d] len[%d] comp_id[%d] data_id[%d]", type, length, comp_id, data_id);

    data_ptr = (int16_t *)&msg[7];

    for (i = 0; i < (length - 4); i++)  {
        if (data_ptr[i] < 0)
            delta = -data_ptr[i];
        delta = -data_ptr[i];

        if (delta > max_delta) {
            ret_val = -1;
            break;
        }
    }

    eph_save_tp_test_data(data_ptr, 3);
    return ret_val;
}

/* check the range between max and min (noise part2) */
static int eph_testing_pt04_process(struct eph_data *ephdata, u8 *msg)
{
    int ret_val = 0;
    int i = 0;
    u8 type = 0;
    u16 length = 0;
    u16 comp_id = 0;
    u8 data_id = 0;
    int16_t *data_ptr = NULL;

    int16_t max_diff = ephdata->pt_max_diff_delta;

    static int16_t max_delta[39*18] = { 0 };
    static int16_t min_delta[39*18] = { 0 };

    if (!msg)
        return -1;

    type = msg[0];
    length = *(u16 *)&msg[1];
    comp_id = *(u16 *)&msg[3];
    data_id = msg[5];

    eswin_log(INFO_LOG, "t[%d] len[%d] comp_id[%d] data_id[%d]", type, length, comp_id, data_id);

    data_ptr = (int16_t *)&msg[7];

    // update max/min delta
    for (i = 0; i < sizeof(max_delta)/2; i++) {
        if (data_ptr[i] > max_delta[i])
            max_delta[i] = data_ptr[i];
        if (data_ptr[i] < min_delta[i])
            min_delta[i] = data_ptr[i];
    }

    for (i = 0; i < sizeof(max_delta)/2; i++) {
        if ((max_delta[i] - min_delta[i]) > max_diff) {
            ret_val = -1;
            break;
        }
    }

    eph_save_tp_test_data(data_ptr, 4);
    return ret_val;
}

static int eph_testing_pt(struct eph_data *ephdata, int pt_index, u8 *buf)
{
    int ret_val = 0;
    int retry_cnt = 2;
    struct tlv_header tlvheader;
    unsigned int enable_flags = 0;
    int need_datasets = 1;

    eswin_log(INFO_LOG, "enter");

    if (pt_index <= 0)
        return -1;

    if (pt_index == 1) {
        enable_flags = BIT(0);
    } else if (pt_index == 2) {
        enable_flags = BIT(0);
    } else if (pt_index == 3) {
        enable_flags = BIT(2);
    } else if (pt_index == 4) {
        enable_flags = BIT(2);
        need_datasets = 5;
    }

    ret_val = eph_testing_preprocess(ephdata, enable_flags);
    if (ret_val) {
        eswin_log(ERR_LOG, "testing preprocess fail %x", ret_val);
        return ret_val;
    }

    /* tic need at least 1000ms to generate the first out */
    msleep(1000);

retry:
    msleep(200);
    memset(buf, 0x0, PAGE_SIZE);
    mutex_lock(&ephdata->comms_mutex);
    ret_val = eph_comms_two_stage_read(ephdata, buf);
    mutex_unlock(&ephdata->comms_mutex);

    if (unlikely(ret_val)) {
        if (retry_cnt) {
            retry_cnt--;
            goto retry;
        }
        eswin_log(ERR_LOG, "read fail %x", ret_val);
        goto exit;
    }

    tlvheader = eph_get_tl_header_info(ephdata, buf);
    if (tlvheader.type != TLV_ENG_DEBUG_DATA) {
        if (retry_cnt) {
            retry_cnt--;
            goto retry;
        }
        eswin_log(ERR_LOG, "read wrong data");
        ret_val = -1;
        goto exit;
    }

    need_datasets--;

    if (pt_index == 1)
        ret_val = eph_testing_pt01_process(ephdata, buf);
    else if (pt_index == 2)
        ret_val = eph_testing_pt02_process(ephdata, buf);
    else if (pt_index == 3)
        ret_val = eph_testing_pt03_process(ephdata, buf);
    else if (pt_index == 4)
        ret_val = eph_testing_pt04_process(ephdata, buf);

    if (need_datasets > 0) {
        retry_cnt = 2;
        goto retry;
    }

    if (ret_val)
        eswin_log(ERR_LOG, "fail %x", ret_val);

exit:
    if (eph_testing_postprocess(ephdata, enable_flags))
        eswin_log(ERR_LOG, "testing postprocess fail");

    return ret_val;
}

int read_line(char **ptr, char *line)
{
	int len = 0;
	char c;

	for (; **ptr == ' ' || **ptr == '\t' || **ptr == '#' || **ptr == '\r' || **ptr == '\n'; *ptr += 1)
		;

	if (**ptr == '\0') {
		return -EPERM;
	}

	while (**ptr != '\0') {
		c = **ptr;
		*ptr += 1;
		if (c != '\n' && c != '\r' && c != ' ') {
			len++;
			*line++ = c;
			*line = '\0';
		} else if (c == '\n') {
			break;
		}
	}

	return len;
}

/* Started by AICoder, pid:e34fbx32c6z96b914d4c0be60034aa25b8414a5f */
int eph_add_ini_keyword(PT_SelftestData selftestdata, char *field, char *key, char *val)
{
    PT_KeyWord pkeyword;

    if (!selftestdata) {
        eswin_log(ERR_LOG, "selftestdata is null");
        return -EPERM;
    }

    if (selftestdata->keyword_num >= EPH_KEYWORD_MAX_NUM) {
        eswin_log(ERR_LOG, "too much keyword");
        return -EPERM;
    }
    pkeyword = &selftestdata->keyword[selftestdata->keyword_num];
    strlcpy(pkeyword->field, field, EPH_KEYWORD_FIELD_MAXLEN);
    strlcpy(pkeyword->key, key, EPH_KEYWORD_KEY_MAXLEN);
    strlcpy(pkeyword->val, val, EPH_KEYWORD_VAL_MAXLEN);

    selftestdata->keyword_num += 1;
    return 0;
}
/* Ended by AICoder, pid:e34fbx32c6z96b914d4c0be60034aa25b8414a5f */

/* Started by AICoder, pid:x60161ec6cpe8c214c9d0b8f70f9671239d632f0 */
int eph_show_ini_keyword(PT_SelftestData selftestdata)
{
    int i;

    if (!selftestdata) {
        eswin_log(ERR_LOG, "selftestdata is null");
        return -EPERM;
    }

    for (i = 0; i < selftestdata->keyword_num; i++) {
        eswin_log(INFO_LOG, "No: %d field:%s, key:%s, val:%s", i,
             selftestdata->keyword[i].field, selftestdata->keyword[i].key, selftestdata->keyword[i].val);
    }

    return 0;
}
/* Ended by AICoder, pid:x60161ec6cpe8c214c9d0b8f70f9671239d632f0 */

int eph_init_keyword(PT_SelftestData selftestdata)
{
	char *ptr;
	char line[EPH_KEYWORD_LINE_MAXLEN];
	char field[EPH_KEYWORD_FIELD_MAXLEN];
	char *kstr;
	int len;
	int i, j;
	char c;

    if ((!selftestdata) || (!selftestdata->csv_file_buf)) {
		eswin_log(ERR_LOG, "selftestdata is null");
		return -EPERM;
    }

	ptr = selftestdata->csv_file_buf;
	memset(field, 0, sizeof(field));
	while (1) {
		memset(line, 0, EPH_KEYWORD_LINE_MAXLEN);
		len = read_line(&ptr, line);
		if (len < 0) {
			break;
		}
		eswin_log(INFO_LOG, "%s", line);

		if (line[0] == '[') {
			j = 0;
			for (i = 1; line[i] != '\0'; i++) {
				c = line[i];
				if (c != ']') {
					field[j++] = c;
					field[j] = '\0';
				}
			}
		} else {
			kstr = strnstr(line, "=", EPH_KEYWORD_LINE_MAXLEN);
			if (kstr == NULL) {
				continue;
			}
			*kstr = 0;
			eph_add_ini_keyword(selftestdata, field, line, kstr + 1);
		}
	}

	eph_show_ini_keyword(selftestdata);

	return 0;
}

/* Started by AICoder, pid:l658fad3ec8c2e3147880a2f20d6062cb2d09203 */
int eph_find_ini_word(PT_SelftestData selftestdata, char *field, char *key)
{
    int i;

    if (!selftestdata) {
        eswin_log(ERR_LOG, "selftestdata is null");
        return -EPERM;
    }

    for (i = 0; i < selftestdata->keyword_num; i++) {
        if (strcmp(selftestdata->keyword[i].field, field) == 0
            && strcmp(selftestdata->keyword[i].key, key) == 0) {
            break;
        }
    }
    if (i == selftestdata->keyword_num) {
        return -EPERM;
    }
    return i;
}
/* Ended by AICoder, pid:l658fad3ec8c2e3147880a2f20d6062cb2d09203 */

/* Started by AICoder, pid:m85f7ff61945b211497d0b428002561372d81ab4 */
int eph_attach_testitem(u8 type, u8 *test_data)
{
    if (!selftestdata) {
        eswin_log(ERR_LOG, "selftestdata is null");
        return -EPERM;
    }

    if (selftestdata->testitem_num >= MAX_TEST_ITEM_NUM) {
        eswin_log(ERR_LOG, "can not add test item");
        return -EPERM;
    }

    selftestdata->testitem[selftestdata->testitem_num].code = type;
    selftestdata->testitem[selftestdata->testitem_num].data = test_data;
    selftestdata->testitem_num++;

    return 0;
}
/* Ended by AICoder, pid:m85f7ff61945b211497d0b428002561372d81ab4 */

/* Started by AICoder, pid:o3d2cv624283b05140670a026012b560de0906c0 */
int eph_init_test_item(void)
{
    int i;
    int val;
    int ret = 0;

    if (!selftestdata) {
        eswin_log(ERR_LOG, "selftestdata is null");
        return -EPERM;
    }

    val = 0;
    i = eph_find_ini_word(selftestdata, KEY_FIELD_ITEM, KEY_ITEM_RAWDATA);
    if (i >= 0) {
        ret = sscanf(selftestdata->keyword[i].val, "%d", &val);
        if (ret < 0)
            eswin_log(ERR_LOG, "get val error");
    }
    if (val == 0) {
        eswin_log(INFO_LOG, "no need rawdata test");
    } else {
        eswin_log(INFO_LOG, "need rawdata test");
        eph_attach_testitem(RAWDATA_TEST_CODE, NULL);
    }

    val = 0;
    i = eph_find_ini_word(selftestdata, KEY_FIELD_ITEM, KEY_ITEM_OPEN_SHORT);
    if (i >= 0) {
        ret = sscanf(selftestdata->keyword[i].val, "%d", &val);
        if (ret < 0)
            eswin_log(ERR_LOG, "get val error");
    }
    if (val == 0) {
        eswin_log(INFO_LOG, "no need open short test");
    } else {
        eswin_log(INFO_LOG, "need open short test");
        eph_attach_testitem(OPEN_SHORT_TEST_CODE, NULL);
    }

    val = 0;
    i = eph_find_ini_word(selftestdata, KEY_FIELD_ITEM, KEY_ITEM_NOISE_TEST_PART1);
    if (i >= 0) {
        ret = sscanf(selftestdata->keyword[i].val, "%d", &val);
        if (ret < 0)
            eswin_log(ERR_LOG, "get val error");
    }
    if (val == 0) {
        eswin_log(INFO_LOG, "no need noise test part1");
    } else {
        eswin_log(INFO_LOG, "need noise test part1");
        eph_attach_testitem(NOISE_TEST_PART1_CODE, NULL);
    }

    val = 0;
    i = eph_find_ini_word(selftestdata, KEY_FIELD_ITEM, KEY_ITEM_NOISE_TEST_PART2);
    if (i >= 0) {
        ret = sscanf(selftestdata->keyword[i].val, "%d", &val);
        if (ret < 0)
            eswin_log(ERR_LOG, "get val error");
    }
    if (val == 0) {
        eswin_log(INFO_LOG, "no need noise test part2");
    } else {
        eswin_log(INFO_LOG, "need noise test part2");
        eph_attach_testitem(NOISE_TEST_PART2_CODE, NULL);
    }

    return 0;
}
/* Ended by AICoder, pid:o3d2cv624283b05140670a026012b560de0906c0 */

/* Started by AICoder, pid:800e8le38ec7d98144240a58406c2f1202d624e6 */
int eph_show_test_item(void)
{
    int i;

    if (!selftestdata) {
        eswin_log(ERR_LOG, "selftestdata is null");
        return -EPERM;
    }

    eswin_log(INFO_LOG, "eph_show_test_item");
    for (i = 0 ; i < selftestdata->testitem_num ; i++) {
        eswin_log(INFO_LOG, "Item %d:%d", i, selftestdata->testitem[i].code);
    }

    return 0;
}
/* Ended by AICoder, pid:800e8le38ec7d98144240a58406c2f1202d624e6 */

/* Started by AICoder, pid:s22f7oa56d0b3d0143f10bfbf0a4f92d14b5f3d8 */
int eph_get_open_short_test_var(struct eph_data *ephdata)
{
    int i = 0;
    int ret = 0;

    if ((!ephdata) || (!selftestdata)) {
        eswin_log(ERR_LOG, "ephdata or selftestdata is null");
        return -EPERM;
    }

    i = eph_find_ini_word(selftestdata, KEY_FIELD_PARMETER, KEY_OPEN_SHORT_THRES);
    if (i < 0) {
        eswin_log(ERR_LOG, "cannot find open test var, use default");
        selftestdata->open_test_var.thres = DEFAULT_OPEN_SHORT_TEST_THRES;
    } else {
        ret = sscanf(selftestdata->keyword[i].val, "%d", &selftestdata->open_test_var.thres);
        if (ret < 0)
            eswin_log(ERR_LOG, "get open_test_var.thres error");
    }

    eswin_log(INFO_LOG, "open test var, threshold:%d", selftestdata->open_test_var.thres);
    ephdata->pt_threshold_add = selftestdata->open_test_var.thres;

    return 0;
}
/* Ended by AICoder, pid:s22f7oa56d0b3d0143f10bfbf0a4f92d14b5f3d8 */

/* Started by AICoder, pid:p1c4dldbb0v36fc143750b978096ef2cd8b512e7 */
int eph_get_rawdata_test_var(struct eph_data *ephdata)
{
    int i = 0;
    int ret = 0;

    if ((!ephdata) || (!selftestdata)) {
        eswin_log(ERR_LOG, "ephdata or selftestdata is null");
        return -EPERM;
    }

    i = eph_find_ini_word(selftestdata, KEY_FIELD_PARMETER, KEY_PARAMETER_RAWDATA);
    if (i < 0) {
        eswin_log(ERR_LOG, "cannot find rawdata max test var, use default");
        selftestdata->rawdata_test_var.thres = DEFAULT_RAWDATA_TEST_THRES;
    } else {
        ret = sscanf(selftestdata->keyword[i].val, "%d", &selftestdata->rawdata_test_var.thres);
        if (ret < 0)
            eswin_log(ERR_LOG, "get rawdata_test_var.thres error");
    }

    eswin_log(INFO_LOG, "rawdata test var, threshold:%d", selftestdata->rawdata_test_var.thres);
    ephdata->pt_flatness_max = selftestdata->rawdata_test_var.thres;

    return 0;
}
/* Ended by AICoder, pid:p1c4dldbb0v36fc143750b978096ef2cd8b512e7 */

/* Started by AICoder, pid:fec2cd42198f6fb14ee0091e30d82a3cb4f71ff9 */
int eph_get_noise_test_var(struct eph_data *ephdata)
{
    int i = 0;
    int ret = 0;

    if ((!ephdata) || (!selftestdata)) {
        eswin_log(ERR_LOG, "ephdata or selftestdata is null");
        return -EPERM;
    }

    i = eph_find_ini_word(selftestdata, KEY_FIELD_PARMETER, KEY_NOISE_TEST_PART1_THRES);
    if (i < 0) {
        eswin_log(ERR_LOG, "cannot find noise test part1 var, use default");
        selftestdata->noise_test_var.thres_part1 = DEFAULT_NOISE_TEST_PART1_THRES;
    } else {
        ret = sscanf(selftestdata->keyword[i].val, "%d", &selftestdata->noise_test_var.thres_part1);
        if (ret < 0)
            eswin_log(ERR_LOG, "get noise_test_var.thres_part1 error");
    }

    i = eph_find_ini_word(selftestdata, KEY_FIELD_PARMETER, KEY_NOISE_TEST_PART2_THRES);
    if (i < 0) {
        eswin_log(ERR_LOG, "cannot find noise test part2 var, use default");
        selftestdata->noise_test_var.thres_part2 = DEFAULT_NOISE_TEST_PART2_THRES;
    } else {
        ret = sscanf(selftestdata->keyword[i].val, "%d", &selftestdata->noise_test_var.thres_part2);
        if (ret < 0)
            eswin_log(ERR_LOG, "get noise_test_var.thres_part2 error");
    }

    eswin_log(INFO_LOG, "noise test var, thres_part1:%d thres_part2:%d",
        selftestdata->noise_test_var.thres_part1, selftestdata->noise_test_var.thres_part2);
    ephdata->pt_max_delta = selftestdata->noise_test_var.thres_part1;
    ephdata->pt_max_diff_delta = selftestdata->noise_test_var.thres_part1;

    return 0;
}
/* Ended by AICoder, pid:fec2cd42198f6fb14ee0091e30d82a3cb4f71ff9 */

/* Started by AICoder, pid:i920379f80z2dc514f8b09bd0076225acd338572 */
int eph_parse_csv(struct eph_data *ephdata)
{
    int ret = 0;
    const struct firmware *csv_file = NULL;

    if (!ephdata) {
        eswin_log(ERR_LOG, "ephdata is null");
        return -EPERM;
    }

    ret = request_firmware(&csv_file, eswin_touch_criteria_name, &ephdata->inputdev->dev);
    if (ret) {
        eswin_log(ERR_LOG, "Could not load csv_file from %s: %d", eswin_touch_criteria_name, ret);
        return ret;
    }
    eswin_log(INFO_LOG, "request csv_file %s success, size=%d", eswin_touch_criteria_name, csv_file->size);

    selftestdata = (PT_SelftestData) kzalloc(sizeof(T_SelftestData), GFP_KERNEL);
    if (selftestdata == NULL) {
        eswin_log(ERR_LOG, "Malloc selftest failed");
        release_firmware(csv_file);
        return -ENOMEM;
    }

    selftestdata->csv_file_buf = kzalloc(csv_file->size + 1, GFP_KERNEL);
    if (selftestdata->csv_file_buf == NULL) {
        eswin_log(ERR_LOG, "malloc csv file buf failed");
        kfree(selftestdata);
        release_firmware(csv_file);
        return -ENOMEM;
    }

    memcpy(selftestdata->csv_file_buf, (char *)csv_file->data, csv_file->size);
    eswin_log(INFO_LOG, "csv_file_buf:%s", selftestdata->csv_file_buf);
    eph_init_keyword(selftestdata);

    eph_init_test_item();
    eph_show_test_item();

    eph_get_open_short_test_var(ephdata);
    eph_get_rawdata_test_var(ephdata);
    eph_get_noise_test_var(ephdata);

    kfree(selftestdata->csv_file_buf);
    selftestdata->csv_file_buf = NULL;

    kfree(selftestdata);
    selftestdata = NULL;

    release_firmware(csv_file);

    return ret;
}
/* Ended by AICoder, pid:i920379f80z2dc514f8b09bd0076225acd338572 */

/* Started by AICoder, pid:n52e42d68de709f14ead088d701581760d703c40 */
/* Self test API */
int eswin_product_self_test(struct eph_data *ephdata)
{
    int ret_val = 0;
    unsigned int count = 0;
    u8 *eng_data_buf = NULL;

	if (!ephdata) {
		eswin_log(ERR_LOG, "ephdata is null");
		return -EPERM;
	}

    if (ephdata->suspended) {
        eswin_log(ERR_LOG, "TIC suspended! Wait wakeup!");
        goto exit;
    }

    ret_val = eph_parse_csv(ephdata);
    if (ret_val) {
        eswin_log(ERR_LOG, "eph_parse_csv failed");
        return -EPERM;
    }

    eng_data_buf = devm_kzalloc(&ephdata->commsdevice->dev, PAGE_SIZE, GFP_KERNEL);
    if (!eng_data_buf)
        goto exit;

    // open/short test
    eswin_log(INFO_LOG, "eph_testing_pt1 open/short test ------------>");
    ret_val = eph_testing_pt(ephdata, 1, eng_data_buf);
    eswin_log(INFO_LOG, "eph_testing_pt1 open/short test <------------, result %d", ret_val);
    if (ret_val) {
        tpd_cdev->zte_tp_selftest_result = tpd_cdev->zte_tp_selftest_result | TP_OPEN_TEST_FAIL | TP_SHORT_TEST_FAIL;
    }
    // TODO: store open/short test data

    memset(eng_data_buf, 0, PAGE_SIZE);
    // rawdata test
    eswin_log(INFO_LOG, "eph_testing_pt2 rawdata test ------------>");
    ret_val = eph_testing_pt(ephdata, 2, eng_data_buf);
    eswin_log(INFO_LOG, "eph_testing_pt2 rawdata test <------------, result %d", ret_val);
    if (ret_val) {
        tpd_cdev->zte_tp_selftest_result = tpd_cdev->zte_tp_selftest_result | TP_RAWDATA_TEST_FAIL;
    }
    // TODO: store rawdata test data

    memset(eng_data_buf, 0, PAGE_SIZE);
    // noise part1
    eswin_log(INFO_LOG, "eph_testing_pt3 noise part1 ------------>");
    ret_val = eph_testing_pt(ephdata, 3, eng_data_buf);
    eswin_log(INFO_LOG, "eph_testing_pt3 noise part1 <------------, result %d", ret_val);
    if (ret_val) {
        tpd_cdev->zte_tp_selftest_result = tpd_cdev->zte_tp_selftest_result | TP_NOISE_TEST_FAIL;
    }
    // TODO: store noise part1 test data

    memset(eng_data_buf, 0, PAGE_SIZE);
    // noise part2
    eswin_log(INFO_LOG, "eph_testing_pt4 noise part2 ------------>");
    ret_val = eph_testing_pt(ephdata, 4, eng_data_buf);
    eswin_log(INFO_LOG, "eph_testing_pt4 noise part2 <------------, result %d", ret_val);
    if (ret_val) {
        tpd_cdev->zte_tp_selftest_result = tpd_cdev->zte_tp_selftest_result | TP_NOISE_TEST_FAIL;
    }
    // TODO: store noise part2 test data

    memset(eng_data_buf, 0, PAGE_SIZE);
    devm_kfree(&ephdata->commsdevice->dev, eng_data_buf);
    eng_data_buf = NULL;

exit:
    return count;
}
/* Ended by AICoder, pid:n52e42d68de709f14ead088d701581760d703c40 */

/* NOTE: create the Product test node for privide test
   specail case from user space
 */

/* TRX Short/Open Test */
ssize_t eph_devattr_test_pt01_show(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    int ret_val = 0;
    unsigned int count = 0;
    unsigned int i = 0;
    struct eph_data *ephdata;
    u8 *eng_data_buf = NULL;

    ephdata = (struct eph_data*)dev_get_drvdata(dev);

    if (ephdata->suspended) {
        eswin_log(ERR_LOG, "TIC suspended! Wait wakeup!");
        goto exit;
    }

    eng_data_buf = devm_kzalloc(dev, PAGE_SIZE, GFP_KERNEL);
    if (!eng_data_buf)
        goto exit;

    ret_val = eph_testing_pt(ephdata, 1, eng_data_buf);

    count += scnprintf(buf, PAGE_SIZE,
            "TEST PT$01: %s\n", (ret_val < 0) ? "fail" : "pass");

    for (i = 0; i < PAGE_SIZE; i++) {
        count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
            eng_data_buf[i]);
    }
    count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

    devm_kfree(dev, eng_data_buf);
    eng_data_buf = NULL;
exit:
    return count;
}

ssize_t eph_devattr_test_pt01_set(struct device *dev,
                                           struct device_attribute *attr,
                                           const char *buf,
                                           size_t count)
{
    int input = 0;
    struct eph_data *ephdata;

    ephdata = (struct eph_data*)dev_get_drvdata(dev);

    if (kstrtoint(buf, 10, &input)) {
        eswin_log(ERR_LOG, "set wrong para");
        return -EINVAL;
    }

    eswin_log(INFO_LOG, "input %d", input);

    if (input < 0)
        return -EINVAL;

    if (input > 0)
        ephdata->pt_threshold_add = input;

    return ephdata->pt_threshold_add;
}

/* rawdata Test */
ssize_t eph_devattr_test_pt02_show(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    int ret_val = 0;
    unsigned int count = 0;
    unsigned int i = 0;
    struct eph_data *ephdata;
    u8 *eng_data_buf = NULL;

    ephdata = (struct eph_data*)dev_get_drvdata(dev);

    if (ephdata->suspended) {
        eswin_log(ERR_LOG, "TIC suspended! Wait wakeup!");
        goto exit;
    }

    eng_data_buf = devm_kzalloc(dev, PAGE_SIZE, GFP_KERNEL);
    if (!eng_data_buf)
        goto exit;

    ret_val = eph_testing_pt(ephdata, 2, eng_data_buf);

    count += scnprintf(buf, PAGE_SIZE,
            "TEST PT$02: %s\n", (ret_val < 0) ? "fail" : "pass");

    for (i = 0; i < PAGE_SIZE; i++) {
        count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
            eng_data_buf[i]);
    }
    count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

    devm_kfree(dev, eng_data_buf);
    eng_data_buf = NULL;
exit:
    return count;
}

ssize_t eph_devattr_test_pt02_set(struct device *dev,
                                           struct device_attribute *attr,
                                           const char *buf,
                                           size_t count)
{
    int input = 0;
    struct eph_data *ephdata;

    ephdata = (struct eph_data*)dev_get_drvdata(dev);

    if (kstrtoint(buf, 10, &input)) {
        eswin_log(ERR_LOG, "set wrong para");
        return -EINVAL;
    }

    if (input < 0)
        return -EINVAL;

    if (input > 0)
        ephdata->pt_flatness_max = input;

    return ephdata->pt_flatness_max;
}

ssize_t eph_devattr_test_pt03_show(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    int ret_val = 0;
    unsigned int count = 0;
    unsigned int i = 0;
    struct eph_data *ephdata;
    u8 *eng_data_buf = NULL;

    ephdata = (struct eph_data*)dev_get_drvdata(dev);

    if (ephdata->suspended) {
        eswin_log(ERR_LOG, "TIC suspended! Wait wakeup!");
        goto exit;
    }

    eng_data_buf = devm_kzalloc(dev, PAGE_SIZE, GFP_KERNEL);
    if (!eng_data_buf)
        goto exit;

    ret_val = eph_testing_pt(ephdata, 3, eng_data_buf);

    count += scnprintf(buf, PAGE_SIZE,
            "TEST PT$03: %s\n", (ret_val < 0) ? "fail" : "pass");

    for (i = 0; i < PAGE_SIZE; i++) {
        count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
            eng_data_buf[i]);
    }
    count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

    devm_kfree(dev, eng_data_buf);
    eng_data_buf = NULL;
exit:
    return count;

}

ssize_t eph_devattr_test_pt03_set(struct device *dev,
                                           struct device_attribute *attr,
                                           const char *buf,
                                           size_t count)
{
    int input = 0;
    struct eph_data *ephdata;

    ephdata = (struct eph_data*)dev_get_drvdata(dev);

    if (kstrtoint(buf, 10, &input)) {
        eswin_log(ERR_LOG, "set wrong para");
        return -EINVAL;
    }

    if (input < 0)
        return -EINVAL;

    if (input > 0)
        ephdata->pt_max_delta = input;

    return ephdata->pt_max_delta;
}

ssize_t eph_devattr_test_pt04_show(struct device *dev,
                                           struct device_attribute *attr,
                                           char *buf)
{
    int ret_val = 0;
    unsigned int count = 0;
    unsigned int i = 0;
    struct eph_data *ephdata;
    u8 *eng_data_buf = NULL;

    ephdata = (struct eph_data*)dev_get_drvdata(dev);

    if (ephdata->suspended) {
        eswin_log(ERR_LOG, "TIC suspended! Wait wakeup!");
        goto exit;
    }

    eng_data_buf = devm_kzalloc(dev, PAGE_SIZE, GFP_KERNEL);
    if (!eng_data_buf)
        goto exit;

    ret_val = eph_testing_pt(ephdata, 4, eng_data_buf);

    count += scnprintf(buf, PAGE_SIZE,
            "TEST PT$04: %s\n", (ret_val < 0) ? "fail" : "pass");

    for (i = 0; i < PAGE_SIZE; i++) {
        count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
            eng_data_buf[i]);
    }
    count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

    devm_kfree(dev, eng_data_buf);
    eng_data_buf = NULL;
exit:
    return count;
}

ssize_t eph_devattr_test_pt04_set(struct device *dev,
                                           struct device_attribute *attr,
                                           const char *buf,
                                           size_t count)
{
    int input = 0;
    struct eph_data *ephdata;

    ephdata = (struct eph_data*)dev_get_drvdata(dev);

    if (kstrtoint(buf, 10, &input)) {
        eswin_log(ERR_LOG, "set wrong para");
        return -EINVAL;
    }

    if (input < 0)
        return -EINVAL;

    if (input > 0)
        ephdata->pt_max_diff_delta = input;

    return ephdata->pt_max_diff_delta;
}
