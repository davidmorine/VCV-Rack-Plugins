﻿#include "mscHack.hpp"
#include "mscHack_Controls.hpp"
#include "dsp/digital.hpp"
#include "CLog.h"

typedef struct
{
    float lp1, bp1;
    float hpIn;
    float lpIn;
    float mpIn;
    
}FILTER_PARAM_STRUCT;

#define L 0
#define R 1

#define DELAY_BUFF_LEN 0x80000

#define MAC_DELAY_SECONDS 1.0f

//-----------------------------------------------------
// Module Definition
//
//-----------------------------------------------------
struct PingPong : Module 
{
	enum ParamIds 
    {
        PARAM_DELAYL,
        PARAM_DELAYR,
        PARAM_LEVEL_FB_LR,
        PARAM_LEVEL_FB_LL,
        PARAM_LEVEL_FB_RL,
        PARAM_LEVEL_FB_RR,
        PARAM_CUTOFF,
        PARAM_Q,
        PARAM_MIX,
        PARAM_FILTER_MODE,
        PARAM_REVERSE,
        nPARAMS
    };

	enum InputIds 
    {
        INPUT_L,
        INPUT_R,
        nINPUTS
	};

	enum OutputIds 
    {
        OUT_L,
        OUT_R,
        nOUTPUTS
	};

    enum FILTER_TYPES
    {
        FILTER_OFF,
        FILTER_LP,
        FILTER_HP,
        FILTER_BP,
     };

    CLog            lg;

    FILTER_PARAM_STRUCT m_Filter[ 2 ];

    float           m_fCutoff = 0.0;
    float           m_LastOut[ 2 ] = {};
    float           m_DelayBuffer[ 2 ][ DELAY_BUFF_LEN ];

    int             m_DelayIn = 0;
    int             m_DelayOut[ 2 ] = {0};

    float           m_fLightReverse = 0.0;
    bool            m_bReverseState = false;

    // Contructor
	PingPong() : Module(nPARAMS, nINPUTS, nOUTPUTS){}

    // Overrides 
	void    step() override;
    json_t* toJson() override;
    void    fromJson(json_t *rootJ) override;
    void    initialize() override;
    void    randomize() override;
    //void    reset() override;

    void    ChangeFilterCutoff( float cutfreq );
    float   Filter( int ch, float in );
};

//-----------------------------------------------------
// MyEQHi_Knob
//-----------------------------------------------------
struct MyCutoffKnob : Green1_Big
{
    PingPong *mymodule;

    void onChange() override 
    {
        mymodule = (PingPong*)module;

        if( mymodule )
        {
            mymodule->ChangeFilterCutoff( value ); 
        }

		RoundKnob::onChange();
	}
};

//-----------------------------------------------------
// MyDelayButton
//-----------------------------------------------------
struct MyDelayButton : Yellow2_Big
{
    int ch;
    float delay;

    PingPong *mymodule;

    void onChange() override 
    {
        mymodule = (PingPong*)module;

        if( mymodule )
        {
            ch = paramId - PingPong::PARAM_DELAYL;

            delay = value * MAC_DELAY_SECONDS * gSampleRate;

            mymodule->m_DelayOut[ ch ] = ( mymodule->m_DelayIn - (int)delay ) & 0x7FFFF;
        }

		RoundKnob::onChange();
	}
};

//-----------------------------------------------------
// MySquareButton_Reverse
//-----------------------------------------------------
struct MySquareButton_Reverse : MySquareButton2
{
    float delay;

    PingPong *mymodule;

    void onChange() override 
    {
        mymodule = (PingPong*)module;

        if( mymodule && value == 1.0 )
        {
            mymodule->m_bReverseState = !mymodule->m_bReverseState;
            mymodule->m_fLightReverse = mymodule->m_bReverseState ? 1.0 : 0.0;

            // recalc delay offsets when going back to forward mode
            if( !mymodule->m_bReverseState )
            {
                delay = mymodule->params[ PingPong::PARAM_DELAYL ].value * MAC_DELAY_SECONDS * gSampleRate;
                mymodule->m_DelayOut[ L ] = ( mymodule->m_DelayIn - (int)delay ) & 0x7FFFF;

                delay = mymodule->params[ PingPong::PARAM_DELAYR ].value * MAC_DELAY_SECONDS * gSampleRate;
                mymodule->m_DelayOut[ R ] = ( mymodule->m_DelayIn - (int)delay ) & 0x7FFFF;
            }
        }

		MomentarySwitch::onChange();
	}
};

//-----------------------------------------------------
// Procedure:   Widget
//
//-----------------------------------------------------
#define Y_OFF_H 40
#define X_OFF_W 40

PingPong_Widget::PingPong_Widget() 
{
	PingPong *module = new PingPong();
	setModule(module);
	box.size = Vec( 15*8, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/PingPong.svg")));
		addChild(panel);
	}

    //module->lg.Open("PingPong.txt");

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365))); 
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));

    // Filter/Res knobs
    addParam(createParam<FilterSelectToggle>( Vec( 66, 55 ), module, PingPong::PARAM_FILTER_MODE, 0.0, 3.0, 0.0 ) );
    addParam(createParam<MyCutoffKnob>( Vec( 23, 60 ), module, PingPong::PARAM_CUTOFF, 0.0, 1.0, 0.0 ) );
    addParam(createParam<Purp1_Med>( Vec( 73, 79 ), module, PingPong::PARAM_Q, 0.0, 1.0, 0.0 ) );
 
    // L Feedback
    addParam(createParam<Red1_Med>( Vec( 49, 110 ), module, PingPong::PARAM_LEVEL_FB_LL, 0.0, 1.0, 0.0 ) );

    // Left
    addInput(createInput<MyPortInSmall>( Vec( 10, 154 ), module, PingPong::INPUT_L ) );
    addParam(createParam<MyDelayButton>( Vec( 38, 143 ), module, PingPong::PARAM_DELAYL, 0.0, 1.0, 0.0 ) );
    addOutput(createOutput<MyPortOutSmall>( Vec( 90, 154 ), module, PingPong::OUT_L ) );

    // R to L level and L to R levels
    addParam(createParam<Red1_Med>( Vec( 9, 191 ), module, PingPong::PARAM_LEVEL_FB_RL, 0.0, 1.0, 0.0 ) );
    addParam(createParam<Red1_Med>( Vec( 9, 226 ), module, PingPong::PARAM_LEVEL_FB_LR, 0.0, 1.0, 0.0 ) );

    // mix knob
    addParam(createParam<Blue2_Med>( Vec( 77, 199 ), module, PingPong::PARAM_MIX, 0.0, 1.0, 0.0 ) );

    // Left
    addInput(createInput<MyPortInSmall>( Vec( 10, 266 ), module, PingPong::INPUT_R ) );
    addParam(createParam<MyDelayButton>( Vec( 38, 255 ), module, PingPong::PARAM_DELAYR, 0.0, 1.0, 0.0 ) );
    addOutput(createOutput<MyPortOutSmall>( Vec( 90, 266 ), module, PingPong::OUT_R ) );

    // R Feedback
    addParam(createParam<Red1_Med>( Vec( 49, 308 ), module, PingPong::PARAM_LEVEL_FB_RR, 0.0, 1.0, 0.0 ) );

    // reverse button
    addParam(createParam<MySquareButton_Reverse>( Vec( 17, 343 ), module, PingPong::PARAM_REVERSE, 0.0, 1.0, 0.0 ) );
    addChild(createValueLight<SmallLight<RedValueLight>>( Vec( 20, 347 ), &module->m_fLightReverse ) );
}

//-----------------------------------------------------
// Procedure:   initialize
//
//-----------------------------------------------------
void PingPong::initialize()
{
    m_fLightReverse = 0.0;
    m_bReverseState = false;
}

//-----------------------------------------------------
// Procedure:   randomize
//
//-----------------------------------------------------
void PingPong::randomize()
{
}

//-----------------------------------------------------
// Procedure:   
//
//-----------------------------------------------------
json_t *PingPong::toJson() 
{
	json_t *rootJ = json_object();

    // reverse state
    json_object_set_new(rootJ, "ReverseState", json_boolean (m_bReverseState));

	return rootJ;
}

//-----------------------------------------------------
// Procedure:   fromJson
//
//-----------------------------------------------------
void PingPong::fromJson(json_t *rootJ) 
{
	// reverse state
	json_t *revJ = json_object_get(rootJ, "ReverseState");

	if (revJ)
		m_bReverseState = json_is_true( revJ );

    m_fLightReverse = m_bReverseState ? 1.0 : 0.0;
}

//-----------------------------------------------------
// Procedure:   ChangeFilterCutoff
//
//-----------------------------------------------------
void PingPong::ChangeFilterCutoff( float cutfreq )
{
    float fx, fx2, fx3, fx5, fx7;

    // clamp at 1.0 and 20/samplerate
    cutfreq = fmax(cutfreq, 20 / gSampleRate); 
    cutfreq = fmin(cutfreq, 1.0);

    // calculate eq rez freq
    fx = 3.141592 * (cutfreq * 0.026315789473684210526315789473684) * 2 * 3.141592; 
    fx2 = fx*fx;
    fx3 = fx2*fx; 
    fx5 = fx3*fx2; 
    fx7 = fx5*fx2;

    m_fCutoff = 2.0 * (fx 
	    - (fx3 * 0.16666666666666666666666666666667) 
	    + (fx5 * 0.0083333333333333333333333333333333) 
	    - (fx7 * 0.0001984126984126984126984126984127));
}

//-----------------------------------------------------
// Procedure:   Filter
//
//-----------------------------------------------------
#define MULTI (0.33333333333333333333333333333333f)
float PingPong::Filter( int ch, float in )
{
    FILTER_PARAM_STRUCT *p;
    float rez, hp1, out = 0.0; 
    float lowpass, highpass, bandpass;

    if( (int)params[ PARAM_FILTER_MODE ].value == 0 )
        return in;

    p = &m_Filter[ ch ];

    rez = 1.0 - params[ PARAM_Q ].value;

    in = in + 0.000000001;

    p->lp1   = p->lp1 + m_fCutoff * p->bp1; 
    hp1      = in - p->lp1 - rez * p->bp1; 
    p->bp1   = m_fCutoff * hp1 + p->bp1; 
    lowpass  = p->lp1; 
    highpass = hp1; 
    bandpass = p->bp1; 

    p->lp1   = p->lp1 + m_fCutoff * p->bp1; 
    hp1      = in - p->lp1 - rez * p->bp1; 
    p->bp1   = m_fCutoff * hp1 + p->bp1; 
    lowpass  = lowpass  + p->lp1; 
    highpass = highpass + hp1; 
    bandpass = bandpass + p->bp1; 

    in = in - 0.000000001;

    p->lp1   = p->lp1 + m_fCutoff * p->bp1; 
    hp1      = in - p->lp1 - rez * p->bp1; 
    p->bp1   = m_fCutoff * hp1 + p->bp1; 

    lowpass  = (lowpass  + p->lp1) * MULTI; 
    highpass = (highpass + hp1) * MULTI; 
    bandpass = (bandpass + p->bp1) * MULTI;

    switch( (int)params[ PARAM_FILTER_MODE ].value )
    {
    case FILTER_LP:
        out = lowpass;
        break;
    case FILTER_HP:
        out = highpass;
        break;
    case FILTER_BP:
        out = bandpass;
        break;
    default:
        break;
    }

    return out;
}

//-----------------------------------------------------
// Procedure:   step
//
//-----------------------------------------------------
void PingPong::step() 
{
    float outL, outR, inL = 0.0, inR = 0.0, inOrigL = 0.0, inOrigR = 0.0;
    bool bMono = false;

    // check right channel first for possible mono
    if( inputs[ INPUT_R ].active )
    {
        inR = clampf( inputs[ INPUT_R ].value / 5.0, -1.0, 1.0 );
        inR = Filter( R, inR );
        inOrigR = inR;
        bMono = false;
    }
    else
        bMono = true;

    // left channel
    if( inputs[ INPUT_L ].active )
    {
        inL = clampf( inputs[ INPUT_L ].value / 5.0, -1.0, 1.0 );
        inL = Filter( L, inL );
        inOrigL = inL;

        if( bMono )
        {
            inOrigR = inL;
            inR = inL;
        }
    }

    m_DelayBuffer[ L ][ m_DelayIn ] = inL + ( m_LastOut[ L ] * params[ PARAM_LEVEL_FB_LL ].value ) + ( m_LastOut[ R ] * params[ PARAM_LEVEL_FB_RL ].value );
    m_DelayBuffer[ R ][ m_DelayIn ] = inR + ( m_LastOut[ R ] * params[ PARAM_LEVEL_FB_RR ].value ) + ( m_LastOut[ L ] * params[ PARAM_LEVEL_FB_LR ].value );

    m_DelayIn = ( ( m_DelayIn + 1 ) & 0x7FFFF );

    outL = m_DelayBuffer[ L ][ m_DelayOut[ L ] ];
    outR = m_DelayBuffer[ R ][ m_DelayOut[ R ] ];

    if( m_bReverseState )
    {
        m_DelayOut[ L ] = ( ( m_DelayOut[ L ] - 1 ) & 0x7FFFF );
        m_DelayOut[ R ] = ( ( m_DelayOut[ R ] - 1 ) & 0x7FFFF );
    }
    else
    {
        m_DelayOut[ L ] = ( ( m_DelayOut[ L ] + 1 ) & 0x7FFFF );
        m_DelayOut[ R ] = ( ( m_DelayOut[ R ] + 1 ) & 0x7FFFF );
    }

    m_LastOut[ L ] = outL;
    m_LastOut[ R ] = outR;

    // output
    outputs[ OUT_L ].value = clampf( ( inOrigL * ( 1.0 - params[ PARAM_MIX ].value ) ) + ( (outL * 5.0) * params[ PARAM_MIX ].value ), -5.0, 5.0 );
    outputs[ OUT_R ].value = clampf( ( inOrigR * ( 1.0 - params[ PARAM_MIX ].value ) ) + ( (outR * 5.0) * params[ PARAM_MIX ].value ), -5.0, 5.0 );
}