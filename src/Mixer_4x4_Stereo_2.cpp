#include "mscHack.hpp"
#include "mscHack_Controls.hpp"
#include "dsp/digital.hpp"
#include "CLog.h"

#define GROUPS 4
#define CH_PER_GROUP 4
#define CHANNELS ( GROUPS * CH_PER_GROUP )
#define nAUX 4

#define GROUP_OFF_X 52
#define CHANNEL_OFF_X 34

#define FADE_MULT (0.0005f)

#define L 0
#define R 1

#define MUTE_FADE_STATE_IDLE 0
#define MUTE_FADE_STATE_INC  1
#define MUTE_FADE_STATE_DEC  2

//-----------------------------------------------------
// Module Definition
//
//-----------------------------------------------------
struct Mix_4x4_Stereo2 : Module 
{
	enum ParamIds 
    {
        PARAM_MAIN_LEVEL,
        PARAM_LEVEL_IN,
        PARAM_PAN_IN            = PARAM_LEVEL_IN + CHANNELS,
        PARAM_GROUP_LEVEL_IN    = PARAM_PAN_IN + CHANNELS,
        PARAM_GROUP_PAN_IN      = PARAM_GROUP_LEVEL_IN + GROUPS,
        PARAM_MUTE_BUTTON       = PARAM_GROUP_PAN_IN + GROUPS,
        PARAM_SOLO_BUTTON       = PARAM_MUTE_BUTTON + CHANNELS,
        PARAM_GROUP_MUTE        = PARAM_SOLO_BUTTON + CHANNELS,
        PARAM_GROUP_SOLO        = PARAM_GROUP_MUTE + GROUPS,

        PARAM_EQ_HI             = PARAM_GROUP_SOLO + GROUPS,
        PARAM_EQ_MD             = PARAM_EQ_HI + CHANNELS,
        PARAM_EQ_LO             = PARAM_EQ_MD + CHANNELS,

        PARAM_AUX_KNOB         = PARAM_EQ_LO + CHANNELS,
        PARAM_AUX_PREFADE      = PARAM_AUX_KNOB + (GROUPS * nAUX),
        PARAM_AUX_OUT          = PARAM_AUX_PREFADE + (GROUPS * nAUX),

        nPARAMS                = PARAM_AUX_OUT + (nAUX)
    };

	enum InputIds 
    {
        IN_LEFT,
        IN_RIGHT                = IN_LEFT + CHANNELS,
        IN_LEVEL                = IN_RIGHT + CHANNELS,
        IN_PAN                  = IN_LEVEL + CHANNELS, 
        IN_GROUP_LEVEL          = IN_PAN + CHANNELS,
        IN_GROUP_PAN            = IN_GROUP_LEVEL + GROUPS,
        nINPUTS                 = IN_GROUP_PAN + GROUPS 
	};

	enum OutputIds 
    {
		OUT_MAINL,
        OUT_MAINR,

        OUT_AUXL,
        OUT_AUXR              = OUT_AUXL + nAUX,

        nOUTPUTS              = OUT_AUXR + nAUX
	};

    CLog            lg;

    // mute buttons
    float           m_fLightMutes[ CHANNELS ] = {};
    bool            m_bMuteStates[ CHANNELS ] = {};
    float           m_fMuteFade[ CHANNELS ] = {};
    
    int             m_FadeState[ CHANNELS ] = {MUTE_FADE_STATE_IDLE};

    // solo buttons
    float           m_fLightSolos[ CHANNELS ] = {};
    bool            m_bSoloStates[ CHANNELS ] = {};

    // group mute buttons
    float           m_fLightGroupMutes[ GROUPS ] = {};
    bool            m_bGroupMuteStates[ GROUPS ] = {};
    float           m_fGroupMuteFade[ GROUPS ] = {};

    int             m_GroupFadeState[ GROUPS ] = {MUTE_FADE_STATE_IDLE};

    // group solo buttons
    float           m_fLightGroupSolos[ GROUPS ] = {};
    bool            m_bGroupSoloStates[ GROUPS ] = {};

    // processing
    bool            m_bMono[ CHANNELS ];
    float           m_fSubMix[ GROUPS ][ 3 ] = {};

    // aux
    bool            m_bGroupPreFadeAuxStates[ GROUPS ][ nAUX ] = {};
    float           m_fLightGroupPreFadeAux[ GROUPS ][ nAUX ] = {};

    // EQ Rez
    float           lp1[ CHANNELS ][ 2 ] = {}, bp1[ CHANNELS ][ 2 ] = {}; 
    float           m_hpIn[ CHANNELS ];
    float           m_lpIn[ CHANNELS ];
    float           m_mpIn[ CHANNELS ];
    float           m_rezIn[ CHANNELS ] = {0};
    float           m_Freq;

#define L 0
#define R 1

    // Contructor
	Mix_4x4_Stereo2() : Module(nPARAMS, nINPUTS, nOUTPUTS){}

    //-----------------------------------------------------
    // MySquareButton_Trig
    //-----------------------------------------------------
    struct MySquareButton_Aux : MySquareButton
    {
        int group, knob, param;

        Mix_4x4_Stereo2 *mymodule;

        void onChange() override 
        {
            mymodule = (Mix_4x4_Stereo2*)module;

            if( mymodule && value == 1.0 )
            {
                param = paramId - Mix_4x4_Stereo2::PARAM_AUX_PREFADE;
                group = param / GROUPS;
                knob  = param - (group * GROUPS);
                mymodule->m_bGroupPreFadeAuxStates[ group ][ knob ] = !mymodule->m_bGroupPreFadeAuxStates[ group ][ knob ];
                mymodule->m_fLightGroupPreFadeAux[ group ][ knob ] = mymodule->m_bGroupPreFadeAuxStates[ group ][ knob ] ? 1.0 : 0.0;
            }

		    MomentarySwitch::onChange();
	    }
    };

    //-----------------------------------------------------
    // MySquareButton_ChMute
    //-----------------------------------------------------
    struct MySquareButton_ChMute : MySquareButton2
    {
        int ch;

        Mix_4x4_Stereo2 *mymodule;

        void onChange() override 
        {
            mymodule = (Mix_4x4_Stereo2*)module;

            if( mymodule && value == 1.0 )
            {
                ch = paramId - Mix_4x4_Stereo2::PARAM_MUTE_BUTTON;
                mymodule->ProcessMuteSolo( ch, true, false );
            }

		    MomentarySwitch::onChange();
	    }
    };

    //-----------------------------------------------------
    // MySquareButton_ChSolo
    //-----------------------------------------------------
    struct MySquareButton_ChSolo : MySquareButton2
    {
        int ch;

        Mix_4x4_Stereo2 *mymodule;

        void onChange() override 
        {
            mymodule = (Mix_4x4_Stereo2*)module;

            if( mymodule && value == 1.0 )
            {
                ch = paramId - Mix_4x4_Stereo2::PARAM_SOLO_BUTTON;

                mymodule->ProcessMuteSolo( ch, false, false );
            }

		    MomentarySwitch::onChange();
	    }
    };

    //-----------------------------------------------------
    // MySquareButton_GroupMute
    //-----------------------------------------------------
    struct MySquareButton_GroupMute : MySquareButton2
    {
        int group;

        Mix_4x4_Stereo2 *mymodule;

        void onChange() override 
        {
            mymodule = (Mix_4x4_Stereo2*)module;

            if( mymodule && value == 1.0 )
            {
                group = paramId - Mix_4x4_Stereo2::PARAM_GROUP_MUTE;

                mymodule->ProcessMuteSolo( group, true, true );
            }

		    MomentarySwitch::onChange();
	    }
    };

    //-----------------------------------------------------
    // MySquareButton_GroupSolo
    //-----------------------------------------------------
    struct MySquareButton_GroupSolo : MySquareButton2
    {
        int group;

        Mix_4x4_Stereo2 *mymodule;

        void onChange() override 
        {
            mymodule = (Mix_4x4_Stereo2*)module;

            if( mymodule && value == 1.0 )
            {
                group = paramId - Mix_4x4_Stereo2::PARAM_GROUP_SOLO;

                mymodule->ProcessMuteSolo( group, false, true );
            }

		    MomentarySwitch::onChange();
	    }
    };

    //-----------------------------------------------------
    // MyEQHi_Knob
    //-----------------------------------------------------
    struct MyEQHi_Knob : Green1_Tiny
    {
        Mix_4x4_Stereo2 *mymodule;
        int param;

        void onChange() override 
        {
            mymodule = (Mix_4x4_Stereo2*)module;

            if( mymodule )
            {
                param = paramId - Mix_4x4_Stereo2::PARAM_EQ_HI;

                mymodule->m_hpIn[ param ] = value; 
            }

		    RoundKnob::onChange();
	    }
    };

    //-----------------------------------------------------
    // MyEQHi_Knob
    //-----------------------------------------------------
    struct MyEQMid_Knob : Green1_Tiny
    {
        Mix_4x4_Stereo2 *mymodule;
        int param;

        void onChange() override 
        {
            mymodule = (Mix_4x4_Stereo2*)module;

            if( mymodule )
            {
                param = paramId - Mix_4x4_Stereo2::PARAM_EQ_MD;
                mymodule->m_mpIn[ param ] = value; 
            }

		    RoundKnob::onChange();
	    }
    };

    //-----------------------------------------------------
    // MyEQHi_Knob
    //-----------------------------------------------------
    struct MyEQLo_Knob : Green1_Tiny
    {
        Mix_4x4_Stereo2 *mymodule;
        int param;

        void onChange() override 
        {
            mymodule = (Mix_4x4_Stereo2*)module;

            if( mymodule )
            {
                param = paramId - Mix_4x4_Stereo2::PARAM_EQ_LO;
                mymodule->m_lpIn[ param ] = value; 
            }

		    RoundKnob::onChange();
	    }
    };

    // Overrides 
	void    step() override;
    json_t* toJson() override;
    void    fromJson(json_t *rootJ) override;
    void    initialize() override;
    void    randomize() override{}
    //void    reset() override;

    void ProcessMuteSolo( int channel, bool bMute, bool bGroup );
    void ProcessEQ( int ch, float *pL, float *pR );
};

#define CUTOFF (0.025f)
//-----------------------------------------------------
// Procedure:   Widget
//
//-----------------------------------------------------
#define AMP_MAX 2.0

Mix_4x4_Stereo2_Widget::Mix_4x4_Stereo2_Widget() 
{
    float fx, fx2, fx3, fx5, fx7;
    int ch, x, y, i, ybase, x2, y2;
	Mix_4x4_Stereo2 *module = new Mix_4x4_Stereo2();
	setModule(module);
	box.size = Vec( 15*47, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/Mix_4x4_Stereo2.svg")));
		addChild(panel);
	}

    //module->lg.Open("Mix_4x4_Stereo2.txt");

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365))); 
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));

    //----------------------------------------------------
    // Add mix sliders
    x = 23;
    y = 38;

    // main channel
	for ( ch = 0; ch < CHANNELS; ch++ ) 
    {
        // Left channel inputs
        addInput(createInput<MyPortInSmall>( Vec( x, y ), module, Mix_4x4_Stereo2::IN_LEFT + ch ) );

        y += 25;

        // Right channel inputs
        addInput(createInput<MyPortInSmall>( Vec( x, y ), module, Mix_4x4_Stereo2::IN_RIGHT + ch ) );

        y += 26;

        // Level knobs
        addParam(createParam<Blue2_Small>( Vec( x - 5, y ), module, Mix_4x4_Stereo2::PARAM_LEVEL_IN + ch, 0.0, AMP_MAX, 0.0 ) );

        y += 31;

        // Level inputs
        addInput(createInput<MyPortInSmall>( Vec( x, y ), module, Mix_4x4_Stereo2::IN_LEVEL + ch ) );

        y += 23;

        // pan knobs
        addParam(createParam<Yellow2_Small>( Vec( x - 5, y ), module, Mix_4x4_Stereo2::PARAM_PAN_IN + ch, -1.0, 1.0, 0.0 ) );

        y += 31;

        // Pan inputs
        addInput(createInput<MyPortInSmall>( Vec( x, y ), module, Mix_4x4_Stereo2::IN_PAN + ch ) );

        y += 22;

        // mute buttons
        addParam(createParam<Mix_4x4_Stereo2::MySquareButton_ChMute>( Vec( x - 7, y ), module, Mix_4x4_Stereo2::PARAM_MUTE_BUTTON + ch, 0.0, 1.0, 0.0 ) );
        addChild(createValueLight<SmallLight<RedValueLight>>( Vec( x - 4, y + 5 ), &module->m_fLightMutes[ ch ] ) );

        //y += 26;

        // solo buttons
        addParam(createParam<Mix_4x4_Stereo2::MySquareButton_ChSolo>( Vec( x + 9, y ), module, Mix_4x4_Stereo2::PARAM_SOLO_BUTTON + ch, 0.0, 1.0, 0.0 ) );
        addChild(createValueLight<SmallLight<GreenValueLight>>( Vec( x + 12, y + 5 ), &module->m_fLightSolos[ ch ] ) );

        y += 22;

        // eq and rez
        addParam(createParam<Mix_4x4_Stereo2::MyEQHi_Knob>( Vec( x, y ), module, Mix_4x4_Stereo2::PARAM_EQ_HI + ch, 0.0, 1.0, 0.5 ) );

        y += 19;

        addParam(createParam<Mix_4x4_Stereo2::MyEQMid_Knob>( Vec( x, y ), module, Mix_4x4_Stereo2::PARAM_EQ_MD + ch, 0.0, 1.0, 0.5 ) );
        
        y += 19;
        
        addParam(createParam<Mix_4x4_Stereo2::MyEQLo_Knob>( Vec( x, y ), module, Mix_4x4_Stereo2::PARAM_EQ_LO + ch, 0.0, 1.0, 0.5 ) );
        
        if( ( ch & 3 ) == 3 )
        {
            x += GROUP_OFF_X;
        }
        else
        {
            x += CHANNEL_OFF_X;
        }

        y = 39;
    }

    // group mixera
    ybase = 278;
    x = 12;
    for( i = 0; i < GROUPS; i++ )
    {
        // mute/solo buttons
        x2 = x + 81;
        y2 = ybase;

        addParam(createParam<Mix_4x4_Stereo2::MySquareButton_GroupMute>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_GROUP_MUTE + i, 0.0, 1.0, 0.0 ) );
        addChild(createValueLight<SmallLight<RedValueLight>>( Vec( x2 + 3, y2 + 4 ), &module->m_fLightGroupMutes[ i ] ) );

        x2 += 28;

        addParam(createParam<Mix_4x4_Stereo2::MySquareButton_GroupSolo>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_GROUP_SOLO + i, 0.0, 1.0, 0.0 ) );
        addChild(createValueLight<SmallLight<GreenValueLight>>( Vec( x2 + 3, y2 + 4 ), &module->m_fLightGroupSolos[ i ] ) );

        // group level and pan inputs
        x2 = x + 79;
        y2 = ybase + 23;

        addInput(createInput<MyPortInSmall>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::IN_GROUP_LEVEL + i ) );

        y2 += 32;

        addInput(createInput<MyPortInSmall>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::IN_GROUP_PAN + i ) );

        // group level and pan knobs
        x2 = x + 105;
        y2 = ybase + 17;

        addParam(createParam<Blue2_Small>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_GROUP_LEVEL_IN + i, 0.0, AMP_MAX, 0.0 ) );

        y2 += 32;

        addParam(createParam<Yellow2_Small>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_GROUP_PAN_IN + i, -1.0, 1.0, 0.0 ) );

        // aux 1/3
#define AUX_H 29
        x2 = x + 6;
        y2 = ybase + 20;
        
        addParam(createParam<Mix_4x4_Stereo2::MySquareButton_Aux>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_AUX_PREFADE + (i * nAUX) + 0, 0.0, 1.0, 0.0 ) );
        addChild(createValueLight<SmallLight<YellowValueLight>>( Vec( x2 + 1, y2 + 2 ), &module->m_fLightGroupPreFadeAux[ i ][ 0 ] ) );

        y2 += AUX_H;
        addParam(createParam<Mix_4x4_Stereo2::MySquareButton_Aux>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_AUX_PREFADE + (i * nAUX) + 2, 0.0, 1.0, 0.0 ) );
        addChild(createValueLight<SmallLight<YellowValueLight>>( Vec( x2 + 1, y2 + 2 ), &module->m_fLightGroupPreFadeAux[ i ][ 2 ] ) );

        x2 = x + 20;
        y2 = ybase + 16;
        addParam(createParam<Red1_Med>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_AUX_KNOB + (i * nAUX) + 0, 0.0, AMP_MAX, 0.0 ) );
        y2 += AUX_H;
        addParam(createParam<Blue3_Med>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_AUX_KNOB + (i * nAUX) + 2, 0.0, AMP_MAX, 0.0 ) );

        // aux 2/4
        x2 = x + 38;
        y2 = ybase + 28;
        
        addParam(createParam<Yellow3_Med>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_AUX_KNOB + (i * nAUX) + 1, 0.0, AMP_MAX, 0.0 ) );
        y2 += AUX_H;
        addParam(createParam<Purp1_Med>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_AUX_KNOB + (i * nAUX) + 3, 0.0, AMP_MAX, 0.0 ) );

        x2 = x + 62;
        y2 = ybase + 32;
        
        addParam(createParam<Mix_4x4_Stereo2::MySquareButton_Aux>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_AUX_PREFADE + (i * nAUX) + 1, 0.0, 1.0, 0.0 ) );
        addChild(createValueLight<SmallLight<YellowValueLight>>( Vec( x2 + 1, y2 + 2 ), &module->m_fLightGroupPreFadeAux[ i ][ 1 ] ) );

        y2 += AUX_H;
        addParam(createParam<Mix_4x4_Stereo2::MySquareButton_Aux>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_AUX_PREFADE + (i * nAUX) + 3, 0.0, 1.0, 0.0 ) );
        addChild(createValueLight<SmallLight<YellowValueLight>>( Vec( x2 + 1, y2 + 2 ), &module->m_fLightGroupPreFadeAux[ i ][ 3 ] ) );

        // account for slight error in pixel conversion to svg area
        x += 155;
    }

    // main mixer knob 
    addParam(createParam<Blue2_Big>( Vec( 633, 237 ), module, Mix_4x4_Stereo2::PARAM_MAIN_LEVEL, 0.0, AMP_MAX, 0.0 ) );

    // outputs
    
    addOutput(createOutput<MyPortOutSmall>( Vec( 636, 305 ), module, Mix_4x4_Stereo2::OUT_MAINL ) );
    addOutput(createOutput<MyPortOutSmall>( Vec( 668, 335 ), module, Mix_4x4_Stereo2::OUT_MAINR ) );

    // AUX out
#define AUX_OUT_H 42
    x2 = 649;
    y2 = 25;

    addParam(createParam<Red1_Med>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_AUX_OUT + 0, 0.0, AMP_MAX, 0.0 ) ); y2 += AUX_OUT_H;
    addParam(createParam<Yellow3_Med>( Vec( x2, y2  ), module, Mix_4x4_Stereo2::PARAM_AUX_OUT + 1, 0.0, AMP_MAX, 0.0 ) ); y2 += AUX_OUT_H;
    addParam(createParam<Blue3_Med>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_AUX_OUT + 2, 0.0, AMP_MAX, 0.0 ) ); y2 += AUX_OUT_H;
    addParam(createParam<Purp1_Med>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::PARAM_AUX_OUT + 3, 0.0, AMP_MAX, 0.0 ) );

    x2 = 635;
    y2 = 45;
    addOutput(createOutput<MyPortOutSmall>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::OUT_AUXL ) ); y2 += AUX_OUT_H;
    addOutput(createOutput<MyPortOutSmall>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::OUT_AUXL + 1 ) );  y2 += AUX_OUT_H;
    addOutput(createOutput<MyPortOutSmall>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::OUT_AUXL + 2 ) ); y2 += AUX_OUT_H;
    addOutput(createOutput<MyPortOutSmall>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::OUT_AUXL + 3 ) );

    x2 = 664;
    y2 = 45;
    addOutput(createOutput<MyPortOutSmall>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::OUT_AUXR ) ); y2 += AUX_OUT_H;
    addOutput(createOutput<MyPortOutSmall>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::OUT_AUXR + 1 ) ); y2 += AUX_OUT_H;
    addOutput(createOutput<MyPortOutSmall>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::OUT_AUXR + 2 ) ); y2 += AUX_OUT_H;
    addOutput(createOutput<MyPortOutSmall>( Vec( x2, y2 ), module, Mix_4x4_Stereo2::OUT_AUXR + 3 ) );

    // calculate eq rez freq
    fx = 3.141592 * (CUTOFF * 0.026315789473684210526315789473684) * 2 * 3.141592; 
    fx2 = fx*fx;
    fx3 = fx2*fx; 
    fx5 = fx3*fx2; 
    fx7 = fx5*fx2;

    module->m_Freq = 2.0 * (fx 
	    - (fx3 * 0.16666666666666666666666666666667) 
	    + (fx5 * 0.0083333333333333333333333333333333) 
	    - (fx7 * 0.0001984126984126984126984126984127));

    module->initialize();
}

//-----------------------------------------------------
// Procedure:   initialize
//
//-----------------------------------------------------
void Mix_4x4_Stereo2::initialize()
{
    int ch, i, aux;

    for( ch = 0; ch < CHANNELS; ch++ )
    {
        m_FadeState[ ch ] = MUTE_FADE_STATE_IDLE;
        m_fLightMutes[ ch ] = 0.0;
        m_fLightSolos[ ch ] = 0.0;
        m_bMuteStates[ ch ] = false;
        m_bSoloStates[ ch ] = false;
        m_fMuteFade[ ch ] = 1.0;
    }

    for( i = 0; i < GROUPS; i++ )
    {
        for( aux = 0; aux < nAUX; aux++ )
        {
            m_bGroupPreFadeAuxStates[ i ][ aux ] = false;
            m_fLightGroupPreFadeAux[ i ][ aux ] = 0.0;
        }

        m_GroupFadeState[ i ] = MUTE_FADE_STATE_IDLE;
        m_fLightGroupMutes[ i ] = 0.0;
        m_fLightGroupSolos[ i ] = 0.0;
        m_bGroupMuteStates[ i ] = false;
        m_bGroupSoloStates[ i ] = false;
        m_fGroupMuteFade[ i ] = 1.0;
    }
}

//-----------------------------------------------------
// Procedure:   
//
//-----------------------------------------------------
json_t *Mix_4x4_Stereo2::toJson() 
{
    bool *pbool;
    json_t *gatesJ;
	json_t *rootJ = json_object();

	// channel mutes
    pbool = &m_bMuteStates[ 0 ];

	gatesJ = json_array();

	for (int i = 0; i < CHANNELS; i++)
    {
		json_t *gateJ = json_integer( (int) pbool[ i ] );
		json_array_append_new( gatesJ, gateJ );
	}

	json_object_set_new( rootJ, "channel mutes", gatesJ );

	// channel solos
    pbool = &m_bSoloStates[ 0 ];

	gatesJ = json_array();

	for (int i = 0; i < CHANNELS; i++)
    {
		json_t *gateJ = json_integer( (int) pbool[ i ] );
		json_array_append_new( gatesJ, gateJ );
	}

	json_object_set_new( rootJ, "channel solos", gatesJ );

	// group mutes
    pbool = &m_bGroupMuteStates[ 0 ];

	gatesJ = json_array();

	for (int i = 0; i < GROUPS; i++)
    {
		json_t *gateJ = json_integer( (int) pbool[ i ] );
		json_array_append_new( gatesJ, gateJ );
	}

	json_object_set_new( rootJ, "group mutes", gatesJ );

	// group solos
    pbool = &m_bGroupSoloStates[ 0 ];

	gatesJ = json_array();

	for (int i = 0; i < GROUPS; i++)
    {
		json_t *gateJ = json_integer( (int) pbool[ i ] );
		json_array_append_new( gatesJ, gateJ );
	}

	json_object_set_new( rootJ, "group solos", gatesJ );

	// AUX states
    pbool = &m_bGroupPreFadeAuxStates[ 0 ][ 0 ];

	gatesJ = json_array();

	for (int i = 0; i < GROUPS * nAUX; i++)
    {
		json_t *gateJ = json_integer( (int) pbool[ i ] );
		json_array_append_new( gatesJ, gateJ );
	}

	json_object_set_new( rootJ, "group AUX prefade states", gatesJ );

	return rootJ;
}

//-----------------------------------------------------
// Procedure:   fromJson
//
//-----------------------------------------------------
void Mix_4x4_Stereo2::fromJson(json_t *rootJ) 
{
    int ch, i, aux;
    bool *pbool;
    json_t *StepsJ;
    bool bSolo[ GROUPS ] = {0}, bGroupSolo = false;

	// channel mutes
    pbool = &m_bMuteStates[ 0 ];

	StepsJ = json_object_get( rootJ, "channel mutes" );

	if (StepsJ) 
    {
		for ( i = 0; i < CHANNELS; i++)
        {
			json_t *gateJ = json_array_get(StepsJ, i);

			if (gateJ)
				pbool[ i ] = json_integer_value( gateJ );
		}
	}

	// channel solos
    pbool = &m_bSoloStates[ 0 ];

	StepsJ = json_object_get( rootJ, "channel solos" );

	if (StepsJ) 
    {
		for ( i = 0; i < CHANNELS; i++)
        {
			json_t *gateJ = json_array_get(StepsJ, i);

			if (gateJ)
				pbool[ i ] = json_integer_value( gateJ );
		}
	}

	// group mutes
    pbool = &m_bGroupMuteStates[ 0 ];

	StepsJ = json_object_get( rootJ, "group mutes" );

	if (StepsJ) 
    {
		for ( i = 0; i < GROUPS; i++)
        {
			json_t *gateJ = json_array_get(StepsJ, i);

			if (gateJ)
				pbool[ i ] = json_integer_value( gateJ );
		}
	}

	// group solos
    pbool = &m_bGroupSoloStates[ 0 ];

	StepsJ = json_object_get( rootJ, "group solos" );

	if (StepsJ) 
    {
		for ( i = 0; i < GROUPS; i++)
        {
			json_t *gateJ = json_array_get(StepsJ, i);

			if (gateJ)
				pbool[ i ] = json_integer_value( gateJ );
		}
	}

    // AUX states
    pbool = &m_bGroupPreFadeAuxStates[ 0 ][ 0 ];

	StepsJ = json_object_get( rootJ, "group AUX prefade states" );

	if (StepsJ) 
    {
		for ( i = 0; i < GROUPS * nAUX; i++)
        {
			json_t *gateJ = json_array_get(StepsJ, i);

			if (gateJ)
				pbool[ i ] = json_integer_value( gateJ );
		}
	}

    // anybody soloing?
    for( ch = 0; ch < CHANNELS; ch++ )
    {
        if( m_bSoloStates[ ch ] )
        {
            bSolo[ ch / CH_PER_GROUP ]  = true;
        }
    }

    for( ch = 0; ch < CHANNELS; ch++ )
    {
        if( bSolo[ ch / CH_PER_GROUP ] )
        {
            // only open soloing channels
            if( m_bSoloStates[ ch ] )
                m_fMuteFade[ ch ] = 1.0;
            else
                m_fMuteFade[ ch ] = 0.0;
        }
        else
        {
            // nobody is soloing so just open the non muted channels
            m_fMuteFade[ ch ] = m_bMuteStates[ ch ] ? 0.0: 1.0;
        }

        m_fLightMutes[ ch ] = m_bMuteStates[ ch ] ? 1.0: 0.0;
        m_fLightSolos[ ch ] = m_bSoloStates[ ch ] ? 1.0: 0.0;
    }

    // anybody group soloing?
    for( i = 0; i < GROUPS; i++ )
    {
        if( m_bGroupSoloStates[ i ] )
        {
            bGroupSolo  = true;
            break;
        }
    }

    for( i = 0; i < GROUPS; i++ )
    {
        for( aux = 0; aux < nAUX; aux++ )
        {
            m_fLightGroupPreFadeAux[ i ][ aux ] = m_bGroupPreFadeAuxStates[ i ][ aux ] ? 1.0: 0.0;
        }

        if( bGroupSolo )
        {
            // only open soloing channels
            if( m_bGroupSoloStates[ i ] )
                m_fGroupMuteFade[ i ] = 1.0;
            else
                m_fGroupMuteFade[ i ] = 0.0;
        }
        else
        {
            // nobody is soloing so just open the non muted channels
            m_fGroupMuteFade[ i ] = m_bGroupMuteStates[ i ] ? 0.0: 1.0;
        }

        m_fLightGroupMutes[ i ] = m_bGroupMuteStates[ i ] ? 1.0: 0.0;
        m_fLightGroupSolos[ i ] = m_bGroupSoloStates[ i ] ? 1.0: 0.0;
    }
}

//-----------------------------------------------------
// Procedure:   ProcessMuteSolo
//
//-----------------------------------------------------
void Mix_4x4_Stereo2::ProcessMuteSolo( int index, bool bMute, bool bGroup )
{
    int i, group, si, ei;
    bool bSoloEnabled = false, bSoloOff = false;

    if( bGroup )
    {
        if( bMute )
        {
            m_bGroupMuteStates[ index ] = !m_bGroupMuteStates[ index ];

            // turn solo off
            if( m_bGroupSoloStates[ index ] )
            {
                bSoloOff = true;
                m_bGroupSoloStates[ index ] = false;
                m_fLightGroupSolos[ index ] = 0.0;
            }

            // if mute is off then set volume
            if( m_bGroupMuteStates[ index ] )
            {
                m_fLightGroupMutes[ index ] = 1.0;
                m_GroupFadeState[ index ] = MUTE_FADE_STATE_DEC;
            }
            else
            {
                m_fLightGroupMutes[ index ] = 0.0;
                m_GroupFadeState[ index ] = MUTE_FADE_STATE_INC;
            }
        }
        else
        {
            m_bGroupSoloStates[ index ] = !m_bGroupSoloStates[ index ];

            // turn mute off
            if( m_bGroupMuteStates[ index ] )
            {
                m_bGroupMuteStates[ index ] = false;
                m_fLightGroupMutes[ index ] = 0.0;
            }

            // shut down volume of all groups not in solo
            if( !m_bGroupSoloStates[ index ] )
            {
                bSoloOff = true;
                m_fLightGroupSolos[ index ] = 0.0;
            }
            else
            {
                m_fLightGroupSolos[ index ] = 1.0;
            }
        }

        // is a track soloing?
        for( i = 0; i < GROUPS; i++ )
        {
            if( m_bGroupSoloStates[ i ] )
            {
                bSoloEnabled = true;
                break;
            }
        }

        if( bSoloEnabled )
        {
            // process solo
            for( i = 0; i < GROUPS; i++ )
            {
                // shut down volume of all groups not in solo
                if( !m_bGroupSoloStates[ i ] )
                {
                    m_GroupFadeState[ i ] = MUTE_FADE_STATE_DEC;
                }
                else
                {
                    m_GroupFadeState[ i ] = MUTE_FADE_STATE_INC;
                }
            }
        }
        // nobody soloing and just turned solo off then enable all channels that aren't muted
        else if( bSoloOff )
        {
            // process solo
            for( i = 0; i < GROUPS; i++ )
            {
                // bring back if not muted
                if( !m_bGroupMuteStates[ i ] )
                {
                    m_GroupFadeState[ i ] = MUTE_FADE_STATE_INC;
                }
            }
        }
    }
    // !bGroup
    else
    {
        group = index / CH_PER_GROUP;

        si = group * CH_PER_GROUP;
        ei = si + CH_PER_GROUP;
        
        if( bMute )
        {
            m_bMuteStates[ index ] = !m_bMuteStates[ index ];

            // turn solo off
            if( m_bSoloStates[ index ] )
            {
                bSoloOff = true;
                m_bSoloStates[ index ] = false;
                m_fLightSolos[ index ] = 0.0;
            }

            // if mute is off then set volume
            if( m_bMuteStates[ index ] )
            {
                m_fLightMutes[ index ] = 1.0;
                m_FadeState[ index ] = MUTE_FADE_STATE_DEC;
            }
            else
            {
                m_fLightMutes[ index ] = 0.0;
                m_FadeState[ index ] = MUTE_FADE_STATE_INC;
            }
        }
        else
        {
            m_bSoloStates[ index ] = !m_bSoloStates[ index ];

            // turn mute off
            if( m_bMuteStates[ index ] )
            {
                m_bMuteStates[ index ] = false;
                m_fLightMutes[ index ] = 0.0;
            }

            // toggle solo
            if( !m_bSoloStates[ index ] )
            {
                bSoloOff = true;
                m_fLightSolos[ index ] = 0.0;
            }
            else
            {
                m_fLightSolos[ index ] = 1.0;
            }
        }

        // is a track soloing?
        for( i = si; i < ei; i++ )
        {
            if( m_bSoloStates[ i ] )
            {
                bSoloEnabled = true;
                break;
            }
        }

        if( bSoloEnabled )
        {
            // process solo
            for( i = si; i < ei; i++ )
            {
                // shut down volume of all not in solo
                if( !m_bSoloStates[ i ] )
                {
                    m_FadeState[ i ] = MUTE_FADE_STATE_DEC;
                }
                else
                {
                    m_FadeState[ i ] = MUTE_FADE_STATE_INC;
                }
            }
        }
        // nobody soloing and just turned solo off then enable all channels that aren't muted
        else if( bSoloOff )
        {
            // process solo
            for( i = si; i < ei; i++ )
            {
                // bring back if not muted
                if( !m_bMuteStates[ i ] )
                {
                    m_FadeState[ i ] = MUTE_FADE_STATE_INC;
                }
            }
        }
    }
}

//-----------------------------------------------------
// Procedure:   ProcessEQ
//
//-----------------------------------------------------
#define MULTI (0.33333333333333333333333333333333f)
void Mix_4x4_Stereo2::ProcessEQ( int ch, float *pL, float *pR )
{
    float rez, hp1; 
    float input[ 2 ], out[ 2 ], lowpass, bandpass, highpass;

    input[ L ] = *pL / 5.0;
    input[ R ] = *pR / 5.0;

    rez = 1.00;

    // do left and right channels
    for( int i = 0; i < 2; i++ )
    {
        input[ i ] = input[ i ] + 0.000000001;

        lp1[ ch ][ i ] = lp1[ ch ][ i ] + m_Freq * bp1[ ch ][ i ]; 
        hp1 = input[ i ] - lp1[ ch ][ i ] - rez * bp1[ ch ][ i ]; 
        bp1[ ch ][ i ] = m_Freq * hp1 + bp1[ ch ][ i ]; 
        lowpass  = lp1[ ch ][ i ]; 
        highpass = hp1; 
        bandpass = bp1[ ch ][ i ]; 

        lp1[ ch ][ i ] = lp1[ ch ][ i ] + m_Freq * bp1[ ch ][ i ]; 
        hp1 = input[ i ] - lp1[ ch ][ i ] - rez * bp1[ ch ][ i ]; 
        bp1[ ch ][ i ] = m_Freq * hp1 + bp1[ ch ][ i ]; 
        lowpass  = lowpass  + lp1[ ch ][ i ]; 
        highpass = highpass + hp1; 
        bandpass = bandpass + bp1[ ch ][ i ]; 

        input[ i ] = input[ i ] - 0.000000001;
        lp1[ ch ][ i ] = lp1[ ch ][ i ] + m_Freq * bp1[ ch ][ i ]; 
        hp1 = input[ i ] - lp1[ ch ][ i ] - rez * bp1[ ch ][ i ]; 
        bp1[ ch ][ i ] = m_Freq * hp1 + bp1[ ch ][ i ]; 

        lowpass  = (lowpass  + lp1[ ch ][ i ]) * MULTI; 
        highpass = (highpass + hp1) * MULTI; 
        bandpass = (bandpass + bp1[ ch ][ i ]) * MULTI;

        out[ i ] = ( highpass * m_hpIn[ ch ] ) + ( lowpass * m_lpIn[ ch ] ) + ( bandpass * m_mpIn[ ch ] );
    }

    *pL = clampf( out[ L ] * 5.0, -5.0, 5.0 );
    *pR = clampf( out[ R ] * 5.0, -5.0, 5.0 );
}

//-----------------------------------------------------
// Procedure:   step
//
//-----------------------------------------------------
void Mix_4x4_Stereo2::step() 
{
    int ch, group, aux;
    float inL = 0.0, inR = 0.0, inLClean, inRClean, outL, outR, mainL = 0.0, mainR = 0.0;
    float inLvl, inPan;
    float auxL[ nAUX ] = {}, auxR[ nAUX ] = {};
    bool bGroupActive[ GROUPS ] = {0};

    memset( m_fSubMix, 0, sizeof(m_fSubMix) );

    // channel mixers
	for ( ch = 0; ch < CHANNELS; ch++ ) 
    {
        group = ch / CH_PER_GROUP;

        inLClean = 0.0;
        inRClean = 0.0;
        inL = 0.0;
        inR = 0.0;

        if( inputs[ IN_RIGHT + ch ].active || inputs[ IN_LEFT + ch ].active )
        {
            inLvl = clampf( ( params[ PARAM_LEVEL_IN + ch ].value + ( inputs[ IN_LEVEL + ch ].normalize( 0.0 ) / 10.0 ) ), 0.0, AMP_MAX ); 

            bGroupActive[ group ] = true;

            // check right channel first for possible mono
            if( inputs[ IN_RIGHT + ch ].active )
            {
                inRClean = inputs[ IN_RIGHT + ch ].value;
                inR = inRClean * inLvl;
                m_bMono[ ch ] = false;
            }
            else
                m_bMono[ ch ] = true;

            // left channel
            if( inputs[ IN_LEFT + ch ].active )
            {
                inLClean = inputs[ IN_LEFT + ch ].value;
                inL = inLClean * inLvl; 

                if( m_bMono[ ch ] )
                {
                    inRClean = inLClean;
                    inR = inL;
                }
            }

            // put output to aux if pre fader
            for ( aux = 0; aux < nAUX; aux++ )
            {
                if( m_bGroupPreFadeAuxStates[ group ][ aux ] )
                {
                    auxL[ aux ] += inLClean * params[ PARAM_AUX_KNOB + (group * nAUX) + aux ].value;
                    auxR[ aux ] += inRClean * params[ PARAM_AUX_KNOB + (group * nAUX) + aux ].value;
                }
            }

            if( m_FadeState[ ch ] == MUTE_FADE_STATE_DEC )
            {
                m_fMuteFade[ ch ] -= FADE_MULT;

                if( m_fMuteFade[ ch ] <= 0.0 )
                {
                    m_fMuteFade[ ch ] = 0.0;
                    m_FadeState[ ch ] = MUTE_FADE_STATE_IDLE;
                }
            }
            else if( m_FadeState[ ch ] == MUTE_FADE_STATE_INC )
            {
                m_fMuteFade[ ch ] += FADE_MULT;

                if( m_fMuteFade[ ch ] >= 1.0 )
                {
                    m_fMuteFade[ ch ] = 1.0;
                    m_FadeState[ ch ] = MUTE_FADE_STATE_IDLE;
                }
            }

            ProcessEQ( ch, &inL, &inR );

            inL *= m_fMuteFade[ ch ];
            inR *= m_fMuteFade[ ch ];

            // pan
            inPan = clampf( params[ PARAM_PAN_IN + ch ].value + ( inputs[ IN_PAN + ch ].normalize( 0.0 ) / 10.0 ), -1.0, 1.0 );

            //lg.f("pan = %.3f\n", inputs[ IN_PAN + ch ].value );

            if( inPan <= 0.0 )
                inR *= ( 1.0 + inPan );
            else
                inL *= ( 1.0 - inPan );

            // put output to aux if not pre fader
            for ( aux = 0; aux < nAUX; aux++ )
            {
                if( !m_bGroupPreFadeAuxStates[ group ][ aux ] )
                {
                    auxL[ aux ] += inL * params[ PARAM_AUX_KNOB + (group * nAUX) + aux ].value;
                    auxR[ aux ] += inR * params[ PARAM_AUX_KNOB + (group * nAUX) + aux ].value;
                }
            }
        }
        // this channel not active
        else
        {

        }

        m_fSubMix[ group ][ L ] += inL;
        m_fSubMix[ group ][ R ] += inR;
    }

    // group mixers
	for ( group = 0; group < GROUPS; group++ ) 
    {
        outL = 0.0;
        outR = 0.0;

        if( bGroupActive[ group ] )
        {
            inLvl = clampf( ( params[ PARAM_GROUP_LEVEL_IN + group ].value + ( inputs[ IN_GROUP_LEVEL + group ].normalize( 0.0 ) / 10.0 ) ), 0.0, AMP_MAX ); 

            outL = m_fSubMix[ group ][ L ] * inLvl;
            outR = m_fSubMix[ group ][ R ] * inLvl;

            // pan
            inPan = clampf( params[ PARAM_GROUP_PAN_IN + group ].value + ( inputs[ IN_GROUP_PAN + group ].normalize( 0.0 ) / 10.0 ), -1.0, 1.0 );

            if( inPan <= 0.0 )
                outR *= ( 1.0 + inPan );
            else
                outL *= ( 1.0 - inPan );

            if( m_GroupFadeState[ group ] == MUTE_FADE_STATE_DEC )
            {
                m_fGroupMuteFade[ group ] -= FADE_MULT;

                if( m_fGroupMuteFade[ group ] <= 0.0 )
                {
                    m_fGroupMuteFade[ group ] = 0.0;
                    m_GroupFadeState[ group ] = MUTE_FADE_STATE_IDLE;
                }
            }
            else if( m_GroupFadeState[ group ] == MUTE_FADE_STATE_INC )
            {
                m_fGroupMuteFade[ group ] += FADE_MULT;

                if( m_fGroupMuteFade[ group ] >= 1.0 )
                {
                    m_fGroupMuteFade[ group ] = 1.0;
                    m_GroupFadeState[ group ] = MUTE_FADE_STATE_IDLE;
                }
            }

            outL *= m_fGroupMuteFade[ group ];
            outR *= m_fGroupMuteFade[ group ];
        }

        mainL += outL;
        mainR += outR;
    }

    // put aux output
    for ( aux = 0; aux < nAUX; aux++ )
    {
        outputs[ OUT_AUXL + aux ].value = clampf( auxL[ aux ] * params[ PARAM_AUX_OUT + aux ].value, -5.0, 5.0 );
        outputs[ OUT_AUXR + aux ].value = clampf( auxR[ aux ] * params[ PARAM_AUX_OUT + aux ].value, -5.0, 5.0 );
    }

    outputs[ OUT_MAINL ].value = clampf( mainL * params[ PARAM_MAIN_LEVEL ].value, -5.0, 5.0 );
    outputs[ OUT_MAINR ].value = clampf( mainR * params[ PARAM_MAIN_LEVEL ].value, -5.0, 5.0 );
}