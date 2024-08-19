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
constexpr size_t size = 0X400;
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
fp highestNote = log2(maxObservableHZ / 8);

class audializer final : public rawSoundStream
{
public:
    sf::Mutex m;
    doubleBuffer audializationBuffer;
    std::vector<colorf> colorValues = std::vector<colorf>(audioBufSize);
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
        audializationBuffer[doubleBuffer::write]->fill(color());

        fillTransformedTexture(drawRect, imageToAudialize, *audializationBuffer[doubleBuffer::write]);
        audializationBuffer.swap();
        // samplesPlayed = 0;
        // m.unlock();
    }
    void setImagePart(const texture &imageToAudialize, rectangle2 rect)
    {
        audializationBuffer[doubleBuffer::write]->fill(color());
        const auto transform = mat3x3::fromRectToRect(rect, (rectangle2)audializationBuffer[doubleBuffer::write]->getClientRect());
        if (rectangle2(imageToAudialize.getClientRect()).cropClientRect(rect))
        {
            fillTransformedBrushRectangle(rect, transform, imageToAudialize, *audializationBuffer[doubleBuffer::write]);
        }
        audializationBuffer.swap();
    }

    constexpr fp getCustomWaveAmpAt(const waveShaper &s, cfp &timePoint)
    {
        return math::mapValue(abs(sin(((timePoint - s.offset) / s.waveLength) * (math::PI))), (fp)0, (fp)1, s.minAmp, s.maxAmp);
    }

    int iterationIndex = 0;
    // amount of samples this audializer played since its construction
    int samplesPlayed = 0;
    waveShaper brightnessWaves = waveShaper(0, soundMin, soundMax);
    virtual bool onGetData(Chunk &data)
    {
        if (rawSoundStream::onGetData(data))
        {
            std::fill(samples.begin(), samples.end(), 0);
            m.lock();

            for (int bufferIndex = 0; bufferIndex < audioBufSize; samplesPlayed++)
            {
                // not % area!
                cfp &exactIterationIndex = (samplesPlayed * (fp)area) / (fp)samplesPerIteration;

                colorf colorsToInterpolate[2]{};
                // not % area!
                cint &iterationIndex0 = (int)floor(exactIterationIndex);
                iterationIndex = iterationIndex0 % area;

                for (int i = 0; i < 2; i++)
                {
                    cint &currentIterationIndex = (iterationIndex + i) % area;
                    cveci2 &imagePos = positions[currentIterationIndex];
                    const color currentColor = audializationBuffer[doubleBuffer::read]->getValueUnsafe(imagePos);
                    colorsToInterpolate[i] = rgb2hsv(currentColor);
                    // if (colorsToInterpolate[i].v() < 0.3)
                    //{
                    //     colorsToInterpolate[i].h() = 1;
                    // }
                }

                cfp &weight = exactIterationIndex - iterationIndex0;
                // the interpolated color
                colorf hsvColor = lerpColor(colorsToInterpolate[0], colorsToInterpolate[1], weight);

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

                // add sawtooth wave for the hue. the sawtooth wave is smaller. it will shift, depending on the hue rotation.

                fp sawToothAmp = brightnessWaves.getSawtoothAmpAt((fp)samplesPlayed);    // * 0.1;
                fp squareAmp = getCustomWaveAmpAt(brightnessWaves, ((fp)samplesPlayed)); // * 0.5;
                //  hue shifts from sine to sawtooth
                //  180 degrees: square <-> sawtooth (more sawtooth the farther)
                fp hueAmp = math::lerp(sawToothAmp, squareAmp, math::absolute(180 - hsvColor.h()) / 180);

                // saturation

                // lesser vibrant = more sine
                cfp &floatAmp = (math::lerp(sineAmp, hueAmp, hsvColor.s())
                                 // better not do much with volume, volume is extremely relative
                                 // alpha
                                 * hsvColor.a());

                if (floatAmp < soundMin || floatAmp > soundMax)
                    throw "";
                amp = (short)floatAmp;

                // if (bufferIndex > 0 && amp > samples[bufferIndex - 1] + 0x1000)
                //{
                //     amp++;
                // }

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
                for (int channelIndex = 0; channelIndex < channelCount; channelIndex++)
                {
                    colorValues[bufferIndex] = hsvColor;
                    samples[bufferIndex++] = amp;
                }
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
    int renderedPosIndex = 0;
    // the image of the list that is being audialized
    size_t imageIndex = 0;
    vec2 eyePos = vec2(size / 2);
    vec2 newEyePos = eyePos;
    // the amount of pixels the eye sees.
    fp eyeFocusSize = 0x100;
    bool changed = false;
    veci2 oldMousePos = veci2();
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
        stream.setImagePart(textures[imageIndex], crectangle2(veci2(eyePos) - (eyeFocusSize / 2), vec2(eyeFocusSize)));
    }
    virtual void keyDown(cvk &keyCode) override
    {
        if (keyCode == sf::Keyboard::Left)
        {
            imageIndex--;
            if (imageIndex == std::wstring::npos)
                imageIndex = textures.size() - 1;
        }
        else if (keyCode == sf::Keyboard::Right)
        {
            imageIndex++;
            imageIndex = math::mod(imageIndex, textures.size());
        }
        changed = true;
    }
    // virtual void mouseMove(cveci2 &position, cmb &button) override
    //{
    //     veci2 difference = position - oldMousePos;
    //     oldMousePos = position;
    //     if (difference.lengthSquared() < 0x10000)
    //     {
    //         eyePos += veci2(difference * math::maximum(eyeFocusSize / size, (fp)1));
    //         changed = true;
    //     }
    //     sf::Mouse::setPosition(sf::Vector2i(0x100, 0x100));
    // }
    virtual void mouseDown(cveci2 &position, cmb &button) override
    {
    }
    virtual void scroll(cveci2 &position, cint &scrollDelta) override
    {
        eyeFocusSize *= pow(2, scrollDelta / -10.0);
        changed = true;
    }
    virtual void render(cveci2 &position, const texture &renderTarget) override
    {
        cint &renderSize = renderTarget.size.minimum();
        if (focused)
        {
            const auto &currentMousePos = sf::Mouse::getPosition();
            const auto &centerPos = sf::Vector2i(0x100, 0x100);
            const auto &difference = currentMousePos - centerPos;
            if (difference.x || difference.y)
            {
                eyePos += veci2(difference.x, -difference.y) * (eyeFocusSize / renderSize);
                changed = true;
                sf::Mouse::setPosition(centerPos);
            }
        }
        if (changed)
        {
            updateStream();
            changed = false;
        }
        renderTarget.fill(colorPalette::black);
        // fp hue = 0;
        cfp &step = 360.0 / area;
        fillTransformedTexture(crectangle2(cvec2(renderSize)), *stream.audializationBuffer[doubleBuffer::read], renderTarget);
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
        // render sound wave
        veci2 offsetPos = veci2(renderSize, 0);
        const int endOffset = math::minimum((int)stream.samples.size(), (int)renderTarget.size.x - offsetPos.x);
        veci2 p0 = offsetPos;
        color c0 = colorPalette::transparent;
        cint& soundWaveSize = renderSize / 0x4;
        for (int sampleOffset = 0; sampleOffset < endOffset; sampleOffset++)
        {
            const color &c = color(hsv2rgb(stream.colorValues[sampleOffset]));
            cveci2 &p1 = offsetPos + veci2(sampleOffset, ((stream.samples[sampleOffset] - (int)soundMin) * (int)soundWaveSize) / ((int)soundMax - soundMin));
            if (c == c0)
            {
                fillLine(renderTarget, p0,
                         p1,
                         solidColorBrush(c));
            }
            else
            {
                renderTarget.setValue(p1, c);
            }
            renderTarget.setValue(veci2(p1.x, p1.y - 1), colorPalette::white);

            p0 = p1;
            c0 = c;
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