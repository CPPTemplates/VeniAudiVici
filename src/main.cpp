#include "application/initialize.h"
#include "application/control/form/form.h"
#include "application/application.h"
#include "gameInfo.h"
#include "math/random/random.h"
#include "soundStream.h"
#include "math/algorithm/iterate2Dto1D.h"
#include "include/math/wave/waveShaper.h"
#include "include/math/sound/sound.h"
#include "include/array/arraynd/arrayndFunctions.h"
#include "include/math/graphics/brush/brushes.h"

enum tileType
{
    air,
    stone,
    danger
};

struct tile
{
    tileType type = air;
};
constexpr size_t size = 0x100;
constexpr size_t area = size * size;
constexpr size_t iterationTimeInMS = 1000;
constexpr size_t samplesPerIteration = (sampleRate * iterationTimeInMS) / 1000;
array2d<tile> tiles = array2d<tile>(veci2(size));
veci2 positions[area]{};

// constexpr fp a4Hz = 440;
//
// constexpr fp a5Hz = a4Hz * 2;
//
// const swingSynchronizer stoneNote = swingSynchronizer(sampleRate / a4Hz, soundMin, soundMax);
// const swingSynchronizer dangerNote = swingSynchronizer(sampleRate / a5Hz, soundMin, soundMax);

fp lowestNote = log2(minObservableHZ * 2);
fp highestNote = log2(maxObservableHZ / 3);

class audializer : public rawSoundStream
{
public:
    sf::Mutex m;
    texture audializationBuffer;
    /// @brief the audializer will loop over this picture and convert it to sound!
    /// @param imageToAudialize will make a copy!
    audializer() : audializationBuffer(veci2(size))
    {
    }
    void setImage(const texture &imageToAudialize)
    {
        rectangle2 drawRect = imageToAudialize.getClientRect();

        // scale to fit
        fp max = drawRect.size.maximum();
        fp scalar = size / max;

        drawRect.size *= scalar;

        // m.lock();
        audializationBuffer.fill(color());

        fillTransformedTexture(drawRect, imageToAudialize, audializationBuffer);
        // samplesPlayed = 0;
        // m.unlock();
    }
    void setImagePart(const texture &imageToAudialize, rectangle2 rect)
    {
        audializationBuffer.fill(color());
        const auto transform = mat3x3::fromRectToRect(rect, (rectangle2)audializationBuffer.getClientRect());
        if (rectangle2(imageToAudialize.getClientRect()).cropClientRect(rect))
        {
            fillTransformedBrushRectangle(rect, transform, imageToAudialize, audializationBuffer);
        }
    }

    size_t iterationIndex = 0;
    // amount of samples this audializer played since its construction
    int samplesPlayed = 0;
    waveShaper brightnessWaves = waveShaper(0, soundMin, soundMax);
    waveShaper hueWaves = waveShaper(0, soundMin, soundMax);
    virtual bool onGetData(Chunk &data)
    {
        if (rawSoundStream::onGetData(data))
        {
            std::fill(samples.begin(), samples.end(), 0);
            m.lock();

            for (int bufferIndex = 0; bufferIndex < audioBufSize; iterationIndex++, samplesPlayed++)
            {
                iterationIndex = (samplesPlayed * (size_t)area) / samplesPerIteration;
                iterationIndex %= area;
                cveci2 &imagePos = positions[iterationIndex];
                const color currentColor = audializationBuffer.getValueUnsafe(imagePos);
                const colorf hsvColor = rgb2hsv(currentColor);
                short amp;
                // higher pitch = lighter color

                // wave length in samples
                cfp &newWaveLength = sampleRate / pow(2, math::lerp(lowestNote, highestNote, hsvColor.v()));
                brightnessWaves.changeWaveLengthAt(samplesPlayed - 1, newWaveLength);

                fp sineAmp = brightnessWaves.getSineAmpAt((fp)samplesPlayed);

                // hue
                //                hueWaves.changeWaveLengthAt(samplesPlayed - 1, newWaveLength * 2 * getNotePitch((hsvColor.h() / 360) * 12));
                // hueWaves.changeWaveLengthAt(samplesPlayed - 1, newWaveLength * 1.5);

                // https://physics.stackexchange.com/questions/194485/which-is-has-the-highest-greatest-sound-intensity-sine-square-or-sawtooth-w
                // multiplying by values, because sines and squares are louder

                //add sawtooth wave for the hue. the sawtooth wave is smaller. it will shift, depending on the hue rotation.

                fp sawToothAmp = brightnessWaves.getSawtoothAmpAt((fp)samplesPlayed); // * 0.1;
                fp squareAmp = brightnessWaves.getSquareAmpAt((fp)samplesPlayed);     // * 0.5;
                //  hue shifts from sine to sawtooth
                //  180 degrees: square <-> sawtooth (more sawtooth the farther)
                fp hueAmp = math::lerp(sawToothAmp, squareAmp, math::absolute(180 - hsvColor.h()) / 180 - 1);

                // saturation

                // lesser vibrant = more sine
                amp = (short)(math::lerp(sineAmp, hueAmp, hsvColor.s())
                              // better not do much with volume, volume is extremely relative
                              // alpha
                              * hsvColor.a());

                // if (currentTile.type == stone)
                //{
                //     amp = (short)stoneNote.getSwingAngle((fp)samplesPlayed);
                // }
                // else if (currentTile.type == danger)
                //{
                //     amp = (short)dangerNote.getSwingAngle((fp)samplesPlayed);
                // }
                // else
                //     amp = rand(currentRandom, soundMin, soundMax);
                samples[bufferIndex++] = amp;
                samples[bufferIndex++] = amp;
            }
            m.unlock();
            return true;
        }
        else
            return false;
    }
};

struct gameForm : public form
{
    std::vector<texture> textures = std::vector<texture>();
    audializer stream = audializer();
    size_t renderedPosIndex = 0;
    // the image of the list that is being audialized
    size_t imageIndex = 0;
    veci2 eyePos = veci2(size / 2);
    //the amount of pixels the eye sees.
    fp eyeFocusSize = 0x100;
    gameForm()
    {
        stdFileSystem::directory_iterator it("data/images");

        for (const auto &file : it)
        {
            textures.push_back(texture(file.path(), true));
        }
        size_t index = 0;
        const auto l = [this, &index](cveci2 &pos)
        {
            positions[index++] = pos;
        };
        iterate2Dto1D(veci2(), size, l);
        for (tile &t : tiles)
        {
            t.type = (tileType)randIndex(currentRandom, 3);
        }
        // if (textures.size())
        //     stream.setImage(textures[0]);
        stream.SetBufSize(audioBufSize, channelCount, sampleRate);
        // play here, because before this we're still modifying the image. as alternative we could also use a mutex
        stream.play();
    }
    void updateStream()
    {
        stream.setImagePart(textures[imageIndex], crectangle2(eyePos - (eyeFocusSize / 2), vec2(eyeFocusSize)));
    }
    virtual void keyDown(cvk &keyCode) override
    {
        if (keyCode == sf::Keyboard::Left)
        {
            imageIndex--;
            if (imageIndex == std::wstring::npos)
                imageIndex = textures.size() - 1;
        }
        else
        {
            imageIndex++;
            imageIndex = math::mod(imageIndex, textures.size());
        }
        updateStream();
    }
    virtual void mouseMove(cveci2 &position, cmb &button) override
    {
        eyePos = position;
        updateStream();
    }
    virtual void mouseDown(cveci2 &position, cmb &button) override
    {
        
    }
    virtual void scroll(cveci2 &position, cint &scrollDelta) override
    {
        eyeFocusSize *= pow(2, scrollDelta / -10.0);
        updateStream();
    }
    virtual void render(cveci2 &position, const texture &renderTarget) override
    {
        renderTarget.fill(colorPalette::black);
        // fp hue = 0;
        cfp &step = 360.0 / area;
        copyArray(stream.audializationBuffer, renderTarget);
        bool renderTillEnd = stream.iterationIndex < renderedPosIndex;
        for (; renderedPosIndex < stream.iterationIndex || renderTillEnd; renderedPosIndex++)
        {
            if (renderTillEnd && renderedPosIndex == area)
            {
                renderedPosIndex = 0;
                renderTillEnd = false;
            }
            veci2 pos = positions[renderedPosIndex];
            const color &val = (color)hsv2rgb(colorf(step * (fp)renderedPosIndex, 1, 1));
            renderTarget.setValue(pos, val);
        }
        // for (const veci2 &pos : positions)
        //{
        //     const color &val = (tiles.getValue(pos).type == air) ? (color)hsv2rgb(colorf(hue += step, 1, 1)) : colorPalette::white;
        //     renderTarget.setValue(pos, val);
        // }
        //  for(color& ptr : renderTarget){
        //      ptr = color((byte)rand(0x100), (byte)rand(0x100), (byte)rand(0x100));
        //  }
        //  renderTarget.fill(color((byte)rand(0x100), (byte)rand(0x100), (byte)rand(0x100)));
    }
};
gameForm *mainForm = new gameForm();
int main(int argc, char *argv[])
{
    // execute this function before you do anything,
    initialize();
    return application(mainForm, gameName).run();
}