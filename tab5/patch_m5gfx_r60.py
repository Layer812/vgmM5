#!/usr/bin/env python3
"""R60 exact-guarded overlay for the M5GFX git revision pinned by R59/R60.

Only modifies PlatformIO's project-local .pio/libdeps/.../M5GFX checkout.
No modification to M5Unified, vgmM5 audio, the hardware, or any other project.
"""
from __future__ import annotations
import hashlib, json, os, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parent
FW=ROOT/'firmware'
ENV='tab5-r61-speaker-revert'
LIB=FW/'.pio'/'libdeps'/ENV/'M5GFX'
FILE_A=LIB/'src'/'M5GFX.cpp'
FILE_B=LIB/'src'/'lgfx'/'v1'/'platforms'/'esp32p4'/'Panel_DSI.inl'
# PlatformIO resolves M5Unified's M5GFX>=0.2.31 dependency independently of
# the explicitly requested 0.2.33 git dependency. On actual Windows R60,
# .pio/libdeps/.../M5GFX was the OFFICIAL M5GFX 0.2.34 release, not 0.2.33.
# Preserve fail-closed checks, but accept precisely these two GitHub-verified
# full-file upstream blob identities; never accept a generic version range.
PINNED_COMMIT='347a23274081a105cb9fc0fc3bbf0e15c78e79c3'
UPSTREAM_M5GFX={
 '0.2.33': {
   'commit': '347a23274081a105cb9fc0fc3bbf0e15c78e79c3',
   'M5GFX_cpp_blob': '483c2c52ca94c25305e4a6b9277afd6e95176dea',
 },
 '0.2.34': {
   'commit': 'a8ebd7bdaae2e9bdc2f3c69c25b83a84d819af72',
   'M5GFX_cpp_blob': '04736f74649d9c14146b6d9ddca871c301546b0d',
 },
}
PANEL_DSI_BLOB='972905af98ffd3806c6a8fc9c87d1bd1a3dad279'
# Panel_DSI.inl has the same exact Git blob in both verified revisions.
MARK='VGMM5_R60_PANEL_COMPAT_20261024'
STATE=FW/'.pio'/'R61_M5GFX_PATCH_SHA256.json'

def sha256(data):return hashlib.sha256(data).hexdigest()
def git_blob(data):return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
def req(condition,message):
 if not condition:raise RuntimeError('R60 PANEL SAFE STOP: '+message)

def once(content,old,new,why):
 n=content.count(old)
 req(n==1,why+': expected 1 exact anchor, got '+str(n))
 return content.replace(old,new,1)

def between(content,start,end,old,new,why):
 a=content.find(start);b=content.find(end,a+len(start))
 req(a>=0 and b>a,why+': branch boundaries missing')
 excerpt=content[a:b]
 req(excerpt.count(old)==1,why+': expected one exact anchor inside branch')
 return content[:a]+excerpt.replace(old,new,1)+content[b:]

def transform_m5(s):
 req('bool found_gt911 = false;' in s,'newer GT911 detection missing; refuse old-patch transplant')
 req('for (int i = 0; i < 60; ++i)' in s,'newer touch retry policy missing')
 s=once(s,
  '            bus_cfg.lane_mbps = hit_st7121 ? 900 : 1040;\n',
  f'            // {MARK}: preserve modern touch identification, only select proven panel lanes.\n'
  '            bus_cfg.lane_mbps = hit_st7121 ? 965 : (found_gt911 ? 730 : 1040);\n',
  'panel-specific DSI lane rate')
 s=between(s,'auto p = new Panel_ILI9881C();','} else if (hit_st7121) {',
  '                det.dpi_freq_mhz = 80;\n',
  f'                // {MARK}: X68KTab5 ILI9881C compatibility pixel clock.\n'
  '                det.dpi_freq_mhz = 60;\n',
  'ILI9881C pixel clock')
 s=between(s,'auto p = new Panel_ST7123();','if (_panel_last == nullptr) {',
  '                det.dpi_freq_mhz = 80;\n',
  f'                // {MARK}: validated ST7123 porch/clock combination.\n'
  '                det.dpi_freq_mhz = 78;\n',
  'ST7123 pixel clock')
 s=between(s,'auto p = new Panel_ST7123();','if (_panel_last == nullptr) {',
  '                det.vsync_back_porch = 8;\n',
  '                det.vsync_back_porch = 4;\n',
  'ST7123 VBP')
 s=between(s,'auto p = new Panel_ST7123();','if (_panel_last == nullptr) {',
  '                // note: back + pulse == 10. If it is out of sync, the display position will shift vertically.\n',
  '                // note: back + pulse == 6. If it is out of sync, the display position will shift vertically.\n',
  'ST7123 display sync comment')
 s=between(s,'auto p = new Panel_ST7123();','if (_panel_last == nullptr) {',
  '                det.vsync_front_porch = 220;\n',
  '                det.vsync_front_porch = 320;\n',
  'ST7123 VFP')
 return s

def transform_dsi(s):
 req('    dpi_config.num_fbs = 2;' in s,'newer M5GFX DoubleFB missing; do not backport')
 s=once(s,
  '#include <esp_lcd_panel_io.h>\n',
  '#include <esp_lcd_panel_io.h>\n'
  f'// {MARK}: P4 MIPI DSI clock/ACK compatibility overlay.\n'
  '#include "hal/mipi_dsi_host_ll.h"\n'
  '#include "soc/mipi_dsi_host_struct.h"\n',
  'P4 DSI low-level headers')
 anchor='    auto ret = esp_lcd_new_panel_dpi(mipi_dsi_bus, &dpi_config, &_disp_panel_handle);\n'
 addition=(
 f'    // {MARK}: only ST7121 and ILI9881C use the ACK-off/sync-pulse policy.\n'
 '    // ST7123 retains the newer pinned M5GFX default, no broad LL changes.\n'
 '    if (ret == ESP_OK)\n'
 '    {\n'
 '      const bool compat_st7121 =\n'
 '           (_config_detail.hsync_back_porch == 40\n'
 '         && _config_detail.hsync_pulse_width == 2\n'
 '         && _config_detail.hsync_front_porch == 40\n'
 '         && _config_detail.vsync_back_porch == 24\n'
 '         && _config_detail.vsync_pulse_width == 20\n'
 '         && _config_detail.vsync_front_porch == 200);\n'
 '      const bool compat_ili9881c =\n'
 '           (_config_detail.hsync_back_porch == 140\n'
 '         && _config_detail.hsync_pulse_width == 40\n'
 '         && _config_detail.hsync_front_porch == 40\n'
 '         && _config_detail.vsync_back_porch == 20\n'
 '         && _config_detail.vsync_pulse_width == 4\n'
 '         && _config_detail.vsync_front_porch == 20);\n'
 '      if (compat_st7121 || compat_ili9881c)\n'
 '      {\n'
 '        mipi_dsi_host_ll_dpi_set_video_burst_type(\n'
 '            &MIPI_DSI_HOST, MIPI_DSI_LL_VIDEO_BURST_WITH_SYNC_PULSES);\n'
 '        mipi_dsi_host_ll_dpi_enable_frame_ack(&MIPI_DSI_HOST, false);\n'
 '      }\n'
 '    }\n')
 s=once(s,anchor,anchor+addition,'DPI panel initialization hook')
 old=(
 '    if (init_dpi(bus) && init_panel())\n'
 '    {\n'
 '        esp_lcd_dpi_panel_get_frame_buffer(_disp_panel_handle, 1, &(_config_detail.buffer));\n'
 '    }\n')
 new=(
 '    if (init_dpi(bus) && init_panel())\n'
 '    {\n'
 f'        // {MARK}: ST7121 compatibility needs DSI clock lane held in HS.\n'
 '        const bool compat_st7121 =\n'
 '             (_config_detail.hsync_back_porch == 40\n'
 '           && _config_detail.hsync_pulse_width == 2\n'
 '           && _config_detail.hsync_front_porch == 40\n'
 '           && _config_detail.vsync_back_porch == 24\n'
 '           && _config_detail.vsync_pulse_width == 20\n'
 '           && _config_detail.vsync_front_porch == 200);\n'
 '        if (compat_st7121)\n'
 '        {\n'
 '          mipi_dsi_host_ll_set_clock_lane_state(\n'
 '              &MIPI_DSI_HOST, MIPI_DSI_LL_CLOCK_LANE_STATE_HS);\n'
 '        }\n'
 '        esp_lcd_dpi_panel_get_frame_buffer(_disp_panel_handle, 1, &(_config_detail.buffer));\n'
 '    }\n')
 s=once(s,old,new,'ST7121 HS clock-lane init hook')
 return s

def audit(a,b):
 req(a.count(MARK)==3,'M5GFX panel overlay absent or duplicated')
 req(b.count(MARK)==3,'Panel_DSI overlay absent or duplicated')
 req('for (int i = 0; i < 60; ++i)' in a,'new touch retries modified')
 req('bool found_gt911 = false;' in a,'GT911 selection missing')
 req('lane_mbps = hit_st7121 ? 965 : (found_gt911 ? 730 : 1040);' in a,
     'panel lane selector not set')
 req('dpi_config.num_fbs = 2;' in b,'newer double buffer lost')
 req('dpi_config.num_fbs = 1;' not in b,'old single buffer found')
 req('mipi_dsi_host_ll_dpi_enable_frame_ack(&MIPI_DSI_HOST, false);' in b,'ACK policy missing')
 req('MIPI_DSI_LL_CLOCK_LANE_STATE_HS' in b,'HS policy missing')
 req('compat_st7121 || compat_ili9881c' in b,'compat is not conditional')
 req('esp_lcd_dpi_panel_get_frame_buffer(_disp_panel_handle, 1, &(_config_detail.buffer))' in b,
     'original frame-buffer ownership changed')
 for start,end,items in [
  ('auto p = new Panel_ILI9881C();','} else if (hit_st7121) {',['det.dpi_freq_mhz = 60;']),
  ('auto p = new Panel_ST7121();','} else if (hit_st7123) {',['det.dpi_freq_mhz = 70;','det.vsync_back_porch = 24;','det.vsync_front_porch = 200;']),
  ('auto p = new Panel_ST7123();','if (_panel_last == nullptr) {',['det.dpi_freq_mhz = 78;','det.vsync_back_porch = 4;','det.vsync_front_porch = 320;'])]:
  i=a.find(start);j=a.find(end,i)
  req(i>=0 and j>i,'panel branch missing: '+start)
  for item in items:req(item in a[i:j],'panel parameter missing '+item)

def apply():
 req(FILE_A.is_file() and FILE_B.is_file(),
     'PlatformIO M5GFX libdeps absent; run pio pkg install first')
 dat_a,dat_b=FILE_A.read_bytes(),FILE_B.read_bytes()
 req(b'\r\n' not in dat_a and b'\r\n' not in dat_b,
     'unexpected CRLF upstream; fail rather than alter source semantics')
 aa,bb=dat_a.decode('utf8'),dat_b.decode('utf8')
 marks=(MARK in aa,MARK in bb)
 if all(marks):
  req(STATE.is_file(),'previously modified vendor library without R60 SHA manifest; stop')
  state=json.loads(STATE.read_text('utf8'))
  req(state.get('pinned_commit')==PINNED_COMMIT,'cached configured pin differs')
  rel=state.get('resolved_release')
  req(rel in UPSTREAM_M5GFX,'cached resolved M5GFX release not supported')
  req(state.get('upstream_git_blob_sha1') == UPSTREAM_M5GFX[rel]['M5GFX_cpp_blob'],
      'cached original full-file identity missing or unexpected')
  req(state.get('M5GFX_sha256')==sha256(dat_a) and state.get('Panel_DSI_sha256')==sha256(dat_b),
      'cached vendor modification does not match recorded R60 patch')
  audit(aa,bb)
  print('PASS R60 vendor overlay already present; resolved M5GFX '+rel+'; verified exact hashes')
  return
 req(not any(marks),'partial/inconsistent M5GFX overlay detected')
 main_blob=git_blob(dat_a)
 candidates=[label for label,item in UPSTREAM_M5GFX.items() if item['M5GFX_cpp_blob']==main_blob]
 req(len(candidates)==1,
     'M5GFX.cpp unexpected original Git blob '+main_blob+
     '; allowed 0.2.33='+UPSTREAM_M5GFX['0.2.33']['M5GFX_cpp_blob']+
     ' or 0.2.34='+UPSTREAM_M5GFX['0.2.34']['M5GFX_cpp_blob'])
 resolved_release=candidates[0]
 dsi_blob=git_blob(dat_b)
 req(dsi_blob==PANEL_DSI_BLOB,
     'Panel_DSI.inl unexpected original Git blob '+dsi_blob+'; expected '+PANEL_DSI_BLOB)
 new_a,new_b=transform_m5(aa),transform_dsi(bb)
 audit(new_a,new_b)
 # All source checks succeed before changing either file.
 FILE_A.write_text(new_a,'utf8',newline='\n')
 FILE_B.write_text(new_b,'utf8',newline='\n')
 payload={'pinned_commit':PINNED_COMMIT,
          'resolved_release':resolved_release,
          'resolved_git_commit':UPSTREAM_M5GFX[resolved_release]['commit'],
          'upstream_git_blob_sha1':main_blob,
          'Panel_DSI_original_git_blob_sha1':dsi_blob,
          'M5GFX_sha256':sha256(FILE_A.read_bytes()),
          'Panel_DSI_sha256':sha256(FILE_B.read_bytes()),
          'mark':MARK}
 audit(FILE_A.read_text('utf8'),FILE_B.read_text('utf8'))
 STATE.write_text(json.dumps(payload,indent=2)+'\n','utf8')
 print('PASS R60 panel overlay: resolved OFFICIAL M5GFX '+resolved_release+
       ' ('+UPSTREAM_M5GFX[resolved_release]['commit']+')')
 print('  Configured direct git dependency: '+PINNED_COMMIT)
 print('  ST7121 : lane965 / 70MHz / V24-20-200 / ACK-off / HS lane')
 print('  ST7123 : lane1040 / 78MHz / V4-2-320 / pinned default ACK/clock')
 print('  ILI9881C with GT911 detected : lane730 / 60MHz / V20-4-20 / ACK-off')
 print('  Unknown touch : old 1040Mbps fallback retained; no conjectural probe override')
 print('  M5GFX GT911 detection, 60 retries, 2 framebuffers: unchanged')

if __name__=='__main__':
 try:apply()
 except Exception as exc:
  print(str(exc),file=sys.stderr)
  sys.exit(2)
