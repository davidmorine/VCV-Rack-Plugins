#include "mscHack.hpp"

Plugin *plugin;

void init(rack::Plugin *p) 
{
	plugin = p;
	plugin->slug = "mscHack";
	plugin->name = "mscHack";
	plugin->homepageUrl = "https://github.com/mschack/VCV-Rack-Plugins";

    createModel<Seq_3x16x16_Widget>     (plugin, "Seq_3ch_16step", "SEQ 3 x 16 Programmable");
    createModel<Seq_Triad_Widget>       (plugin, "TriadSeq", "SEQ Triad Programmable");
    createModel<SynthDrums_Widget>      (plugin, "SynthDrums", "SYNTH Drums");
    createModel<Mix_1x4_Stereo_Widget>  (plugin, "Mix_1x4_Stereo", "MIXER 1x4 Stereo/Mono");
    createModel<Mix_2x4_Stereo_Widget>  (plugin, "Mix_2x4_Stereo", "MIXER 2x4 Stereo/Mono");
    createModel<Mix_4x4_Stereo2_Widget> (plugin, "Mix_4x4_Stereo(2)", "MIXER 4x4 Stereo/Mono");
    createModel<Mix_4x4_Stereo_Widget>  (plugin, "Mix_4x4_Stereo", "MIXER 4x4 (old)");
    createModel<PingPong_Widget>        (plugin, "PingPong_Widget", "DELAY Ping Pong");
    createModel<Osc_3Ch_Widget>         (plugin, "Osc_3Ch_Widget", "OSC 3 Channel");
}
