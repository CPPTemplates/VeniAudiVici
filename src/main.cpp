#include "application/initialize.h"
#include "application/control/form/form.h"
#include "include/application/application.h"
#include "gameInfo.h"
#include "math/random/random.h"
#include "soundStream.h"
#include "include/math/algorithm/iterate2Dto1D.h"
#include "include/math/swingsynchronizer.h"
#include "include/math/sound/sound.h"

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
constexpr int size = 0x400;
constexpr int area = size * size;
array2d<tile> tiles = array2d<tile>(veci2(size));
veci2 positions[area]{};

//constexpr fp a4Hz = 440;
//
//constexpr fp a5Hz = a4Hz * 2;
//
//const swingSynchronizer stoneNote = swingSynchronizer(sampleRate / a4Hz, soundMin, soundMax);
//const swingSynchronizer dangerNote = swingSynchronizer(sampleRate / a5Hz, soundMin, soundMax);

fp lowestNote = log2(minObservableHZ);
fp highestNote = log2(maxObservableHZ);

class audializer : public rawSoundStream
{
public:
    texture imageToAudialize;
    /// @brief the audializer will loop over this picture and convert it to sound!
    /// @param imageToAudialize will make a copy!
    audializer(const texture &imageToAudialize) : imageToAudialize(imageToAudialize)
    {
    }

protected:
    size_t iterationIndex = 0;
    // amount of samples this audializer played since its construction
    int samplesPlayed = 0;
    swingSynchronizer sineSynchronizer = swingSynchronizer(0, soundMin, soundMax);
    virtual bool onGetData(Chunk &data)
    {
        if (rawSoundStream::onGetData(data))
        {
            std::fill(samples.begin(), samples.end(), 0);

            for (int bufferIndex = 0; bufferIndex < audioBufSize; iterationIndex++, samplesPlayed++)
            {
                size_t iterationIndex = (samplesPlayed * (size_t)area) / sampleRate;
                iterationIndex %= area;
                const color currentColor = imageToAudialize.getValue(positions[iterationIndex]);
                const colorf hsvColor = rgb2hsv(currentColor);
                short amp;
                // higher pitch = lighter color

                sineSynchronizer.swingDuration = pow(2, math::lerp(lowestNote, highestNote, hsvColor.v()));
                amp = (short)sineSynchronizer.getSwingAngle((fp)samplesPlayed);
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
            return true;
        }
        else
            return false;
    }
};

struct gameForm : public form
{
    gameForm()
    {
        size_t index = 0;
        const auto l = [this, &index](cveci2 &pos)
        {
            positions[index++] = pos;
        };
        iterate2Dto1D(veci2(), size, directionID::positiveX, l);
        for (tile &t : tiles)
        {
            t.type = (tileType)randIndex(currentRandom, 3);
        }
    }
    virtual void render(cveci2 &position, const texture &renderTarget) override
    {
        fp hue = 0;
        cfp &step = 360.0 / area;
        for (const veci2 &pos : positions)
        {
            const color &val = (tiles.getValue(pos).type == air) ? (color)hsv2rgb(colorf(hue += step, 1, 1)) : colorPalette::white;
            renderTarget.setValue(pos, val);
        }
        // for(color& ptr : renderTarget){
        //     ptr = color((byte)rand(0x100), (byte)rand(0x100), (byte)rand(0x100));
        // }
        // renderTarget.fill(color((byte)rand(0x100), (byte)rand(0x100), (byte)rand(0x100)));
    }
};
gameForm *mainForm = new gameForm();
int main(int argc, char *argv[])
{
    const texture &tex = texture(L"data/images/biker.jpg", true);
    audializer stream(tex);
    stream.SetBufSize(audioBufSize, channelCount, sampleRate);
    stream.play();
    // execute this function before you do anything,
    initialize();
    return application(mainForm, gameName).run();
}