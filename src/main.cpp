// https://docs.m5stack.com/en/api/m5paper/system_api
#include <Arduino.h>
#include <M5EPD.h>
#include "DSEG7_Classic_Mini_Regular_60.h"
#include "frame.h"
#include "SPIFFS.h"
#include "prog_quotes.h"
#include <WiFi.h>
#include <esp_wifi.h>

const bool debugMode = false;
const bool debugMessageSync = false;
uint8_t *screenBuffer;
const int screenWidth = 540;
const int screenHeight = 960;

struct area
{
    int x;
    int y;
    int width;
    int height;
};

const struct area screenArea = {0, 0, screenWidth, screenHeight};
struct area screenUpdateRange;

void debug(String msg)
{
    if (debugMode)
    {
        Serial.println("[" + String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)) + "] " + String(millis()) + ": " + msg);
        if (debugMessageSync)
        {
            Serial.flush();
        }
    }
}

class UiObj
{
public:
    enum objectEvents
    {
        EVENT_NONE,
        EVENT_TOUCH,
        EVENT_DRAW,
        EVENT_UPDATE,
        EVENT_VISIBILITY,
        EVENT_PARENT_CHANGE
    };

    UiObj()
    {
        this->x = 0;
        this->y = 0;
        this->width = 0;
        this->height = 0;
        this->surface = NULL;
        this->hardwareDraw = false;
        this->unbuffered = true;
        this->initDefaults();
    }

    UiObj(int x, int y, int width, int height, bool unbuffered = false)
    {
        this->init(x, y, width, height, unbuffered);
    };

    void init(int x, int y, int width, int height, bool unbuffered = false)
    {
        this->x = x;
        this->y = y;
        this->width = width;
        this->height = height;
        this->surface = TFT_eSprite(NULL);
        if (!unbuffered && width > 0 && height > 0)
        {
            this->surface.setColorDepth(8);
            this->surface.createSprite(width, height, 1);
            this->unbuffered = false;
        }
        else
        {
            this->unbuffered = true;
        }
        this->hardwareDraw = unbuffered;
        this->initDefaults();
    };

    /**
     * @brief Sets the default values for the object.
     */
    void initDefaults()
    {
        this->visible = true;
        this->layer = LAYER_CENTRE;
        this->updated = true;
        this->initialised = true;
        this->visibilityChanged = false;
        this->parent = NULL;
        this->newParent = false;
        this->drawn = false;
        this->exposed = false;
        this->updateArea = {0, 0, 0, 0};
    };

    /**
     * @brief Create a buffer for the object.
     *
     * @param width The pixel width of the buffer.
     * @param height The pixel height of the buffer.
     */
    void createBuffer(int width, int height)
    {
        if (this->unbuffered)
        {
            this->width = width;
            this->height = height;
            this->surface.setColorDepth(8);
            this->surface.createSprite(this->width, this->height, 1);
            this->unbuffered = false;
            this->hardwareDraw = false;
        }
    };

    /**
     * @brief Drawing method for the object.
     * @details This method should draw the object to the screen buffer.
     * The property this->surface is a TFT_eSprite object that can be using to draw on.
     * The child class must implement this.
     */
    virtual void draw() = 0;

    /**
     * @brief Called when the object is touched by the user.
     *
     * @param x The x coordinate on the object that was touched.
     * @param y The y coordinate on the object that was touched.
     * @return true
     * @return false
     */
    virtual bool touchEvent(int x, int y) = 0;

    /**
     * @brief Finds the area on the object that has been updated and returns it.
     * The child class must implement this.
     * @return struct area
     */
    virtual struct area getUpdateArea() = 0;

    /**
     * @brief Set the parent to this object.
     * @details The parent object is the object that contains this object.
     * The object will be drawn to the parent object's buffer.
     * If the parent is NULL then the object will be drawn directly to the screen.
     * @param parent The containing object.
     */
    void setParent(UiObj *parent)
    {
        this->parent = parent;
        this->newParent = true;
    };

    /**
     * @brief Checks if an area on the parent is within the bounds of an object.
     *
     * @param area The area to check against the object.
     * @return true The area is within the object.
     * @return false The area is partially or completely outside the object.
     */
    virtual bool within(struct area area)
    {
        if (area.x >= this->x && area.x < this->x + this->width && area.y >= this->y && area.y < this->y + this->height)
        {
            if (area.x + area.width <= this->x + this->width && area.y + area.height <= this->y + this->height)
            {
                return true;
            }
            else
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    };

    struct area childOffset(struct area area)
    {
        struct area result;
        if (this->parent == NULL)
        {
            return area;
        }
        else
        {
            result.x = area.x - this->x;
            result.y = area.y - this->y;
            result.width = area.width;
            result.height = area.height;
            return result;
        }
    };

    /**
     * @brief Sets the object as visible.
     */
    void show()
    {
        this->visible = true;
        this->visibilityChanged = true;
    };

    /**
     * @brief  Follows the parent chain to find the top level object.
     * @return UiObj* The top level object.
     */
    UiObj *topLevel()
    {
        UiObj *obj = this;
        while (true)
        {
            if (obj->parent == NULL)
            {
                return obj;
            }
            else
            {
                obj = obj->parent;
            }
        };
    };

    struct area clip(struct area area1)
    {
        return area1;
    }

    struct area clip(struct area area1, struct area area2)
    {
        struct area result;
        result.x = max(area1.x, area2.x);
        result.y = max(area1.y, area2.y);
        result.width = min(area1.x + area1.width, area2.x + area2.width) - result.x;
        result.height = min(area1.y + area1.height, area2.y + area2.height) - result.y;
        return result;
    };

    template <typename... Args>
    struct area clip(struct area area1, struct area area2, Args... others)
    {
        return this->clip(this->clip(area1, area2), others...);
    };

    template <typename... Args>
    struct area merge(struct area area, struct area others)
    {
        struct area result = this->merge(area, others);
        result.x = min(result.x, area.x);
        result.y = min(result.y, area.y);
        result.width = max(result.width, area.x + area.width);
        result.height = max(result.height, area.y + area.height);
        return result;
    };

    struct area merge(struct area area)
    {
        struct area result;
        result.x = min(this->width, area.x);
        result.y = min(this->height, area.y);
        result.width = max(this->width, area.x + area.width);
        result.height = max(this->height, area.y + area.height);
        return result;
    }

    /// @brief Finds if two objects overlap on the screen.
    /// @param otherObj The object to check against.
    /// @return The overlapping area. The size (height and width) will be 0 if there is no overlap.
    struct area overlap(UiObj *otherObj)
    {
        struct area thisArea = this->getAbsolutePos();
        struct area otherArea = otherObj->getAbsolutePos();
        if (thisArea.x + thisArea.width < otherArea.x || thisArea.x > otherArea.x + otherArea.width || thisArea.y + thisArea.height < otherArea.y || thisArea.y > otherArea.y + otherArea.height)
        {
            return {0, 0, 0, 0};
        }
        else
        {
            return {max(thisArea.x, otherArea.x), max(thisArea.y, otherArea.y), min(thisArea.x + thisArea.width, otherArea.x + otherArea.width) - max(thisArea.x, otherArea.x), min(thisArea.y + thisArea.height, otherArea.y + otherArea.height) - max(thisArea.y, otherArea.y)};
        }
    };

    /// @brief Find if an area overlaps with the object.
    /// @param area The area to check, relative to the object.
    /// @return The overlapping area. The size (height and width) will be 0 if there is no overlap.
    struct area overlap(struct area area)
    {
        struct area thisArea = this->getAbsolutePos();
        if (thisArea.x + thisArea.width < area.x || thisArea.x > area.x + area.width || thisArea.y + thisArea.height < area.y || thisArea.y > area.y + area.height)
        {
            return {0, 0, 0, 0};
        }
        else
        {
            return {max(thisArea.x, area.x), max(thisArea.y, area.y), min(thisArea.x + thisArea.width, area.x + area.width) - max(thisArea.x, area.x), min(thisArea.y + thisArea.height, area.y + area.height) - max(thisArea.y, area.y)};
        }
    };

    /// @brief Finds the position of the object relative to its parent.
    /// @return An offset position with the parents co-ordinates added.
    struct area offsetToParent()
    {
        if (this->parent)
        {
            struct area parentArea = this->parent->getArea();
            return {this->x + parentArea.x, this->y + parentArea.y, this->width, this->height};
        }
        else
        {
            return this->getArea();
        }
    }

    /// @brief Finds the absolute position of an object on the screen.
    /// @return An area struct with the X and Y co-ordinates.
    struct area getAbsolutePos()
    {
        if (this->parent)
        {
            struct area parentArea = this->parent->getAbsolutePos();
            return {this->x + parentArea.x, this->y + parentArea.y, this->width, this->height};
        }
        else
        {
            return this->getArea();
        }
    }

    /// @brief Called when part of an object is uncovered and its buffer needs to be rerendered to the parent.
    /// @param xArea A struct containing the area of the object that needs to be redrawn.
    virtual void expose(struct area xArea)
    {
        this->exposed = true;
        this->exposeArea = xArea;
    };

    void hide()
    {
        if (this->visible == true)
        {
            this->visible = false;
            this->visibilityChanged = true;
            this->topLevel()->expose(this->getArea());
        }
    };

    /// @brief Resets the flags used to render the object.
    /// @details This function is called after the object has been rendered to reset the flags used to render the object.
    /// This can be overridden by child classes to reset any additional flags.
    virtual void resetStatus()
    {
        if (this->drawn)
        {
            this->updated = false;
            this->drawn = false;
            this->exposed = false;
        }
        this->newParent = false;
        this->visibilityChanged = false;
    };

    /// @brief  Sets the object position within it's container.
    /// @param  x The x position of the object.
    /// @param  y The y position of the object.
    /// @param relative If true the position is relative to the current position.
    void move(int x, int y, bool relative = false)
    {
        if (this->parent)
            this->parent->expose(this->getArea());
        if (relative)
        {
            this->x += x;
            this->y += y;
        }
        else
        {
            this->x = x;
            this->y = y;
        }
        if (this->parent)
            this->parent->expose(this->getArea());
    };

    /// @brief  Checks if the object has been updated since the last render.
    /// @details This function is used to determine if the object needs to be redrawn.
    /// The function should be overridden by child classes to check for changes to the object.
    /// @return True if the object has been updated.
    virtual bool isUpdated() = 0;

    virtual bool visualChange()
    {
        return this->visibilityChanged || this->newParent || this->isUpdated() || this->exposed || (this->parent && this->parent->visibilityChanged);
    };
    /// @brief Converts the object properties to an area struct.
    /// @return An area struct with the object area.
    struct area getArea()
    {
        struct area area;
        area.x = this->x;
        area.y = this->y;
        area.width = this->width;
        area.height = this->height;
        return area;
    };

    /// @brief Draws and visible or changed objects to their surface.
    /// @details This function controls the rendering pipeline for UiObj types and calls the custom draw() function if an object is changed.
    void render()
    {
        if (!this->initialised || !this->visible)
        {
            return;
        }

        this->getUpdateArea();
        if (this->isUpdated())
        {
            debug("Drawing object: " + String((uint32_t)this) + " At: " + String(this->x) + ", " + String(this->y) + ", " + String(this->width) + ", " + String(this->height));
            this->draw();
        }
        else
        {
            debug("Object not updated, skipping draw");
        }

        if (this->visualChange())
        {
            debug("Object updated, pushing to parent");
            if (this->parent == NULL || this->parent->hardwareDraw)
            {
                debug("Software draw (pushing buffer)");
                this->getUpdateArea();
                this->packToGrey(this->updateArea);
                if (this->exposed)
                {
                    // this->packToGrey(this->exposeArea);
                }
                debug("Pushing canvas to screen");
                if (updateArea.width == 0 || updateArea.height == 0)
                {
                    debug("Update area is 0, skipping");
                    return;
                }
            }
            else if (this->parent != NULL)
            {
                TFT_eSprite *parentBuffer = &(this->parent->surface);
                debug("Software draw (copying to parent)");
                this->copyToParent();
            }
            else
            {
                debug("Cannot draw, no parent or hardware buffer.");
            }
            debug("Software draw (done)");
        }
        else
        {
            debug("Object not updated, skipping draw");
        }
        this->drawn = true;
    };

    /// @brief  Copies the object's drawing buffer to the parent buffer.
    /// @details Used for drawing objects inside containers.
    void copyToParent()
    {
        TFT_eSprite *buffer = &this->parent->surface;
        uint8_t *ownBuf = (uint8_t *)this->surface.frameBuffer(1);
        debug("Source buffer: " + String((uint32_t)ownBuf) + ", Dest buffer: " + String((uint32_t)buffer->frameBuffer(1)));
        if (buffer->frameBuffer(1) == NULL)
        {
            debug("Parent buffer is NULL, NOT copying");
            return;
        }
        int height = min(this->height, buffer->height() - this->y);
        int width = min(this->width, buffer->width() - this->x);
        debug("Copying object: " + String((uint32_t)this) + " to size: " + String(width) + "x" + String(height) + " at: " + String(this->x) + ", " + String(this->y));
        debug("Buffer size: " + String(buffer->width()) + "x" + String(buffer->height()));
        for (int y = 0; y < height; y++)
        {
            memcpy((uint8_t *)buffer->frameBuffer(1) + (y + this->y) * buffer->width() + this->x, ownBuf + y * this->width, width);
        }
    };

    /// @brief Pack the object to the screen buffer.
    /// @details Copies an objects 8-bit drawing buffer in the screen buffer transforming it to 4-bit greyscale.
    void packToGrey(struct area renderArea)
    {
        debug("Render area is: " + String(renderArea.x) + ", " + String(renderArea.y) + ", " + String(renderArea.width) + ", " + String(renderArea.height));
        // renderArea = this->clip(this->getArea(), renderArea, screenArea);

        uint8_t *ownBuf = (uint8_t *)this->surface.frameBuffer(1);
        uint8_t *outBuf = (uint8_t *)screenBuffer;
        struct area absoluteRange = this->getAbsolutePos();
        struct area topRange = this->topLevel()->updateArea;
        if (ownBuf == NULL || outBuf == NULL)
        {
            return;
        }

        debug("Packing object: " + String((uint32_t)this) + " to size: " + String(renderArea.width) + "x" + String(renderArea.height) + " at: " + String(renderArea.x) + ", " + String(renderArea.y));
        debug("Object size: " + String(this->width) + "x" + String(this->height));
        debug("Position: " + String(this->x) + ", " + String(this->y));
        debug("Screen update area: " + String(screenUpdateRange.x) + ", " + String(screenUpdateRange.y) + ", " + String(screenUpdateRange.width) + ", " + String(screenUpdateRange.height));
        debug("Absolute area is " + String(absoluteRange.x) + ", " + String(absoluteRange.y) + ", " + String(absoluteRange.width) + ", " + String(absoluteRange.height));
        if (this->parent)
        {
            debug("Parent buffer size: " + String(this->parent->width) + "x" + String(this->parent->height));
            debug("Parent buffer update area: " + String(this->parent->updateArea.x) + ", " + String(this->parent->updateArea.y) + ", " + String(this->parent->updateArea.width) + ", " + String(this->parent->updateArea.height));
        }
        else
        {
            debug("No parent buffer");
        }
        int ownBufOffset = 0;
        int outBufOffset = 0;
        for (int y = 0; y < renderArea.height; y++)
        {
            ownBufOffset = ((y + renderArea.y) * this->width) + renderArea.x;
            outBufOffset = (((y + this->y - topRange.y) * topRange.width) + this->x - topRange.x) / 2;
            // debug("   OWN: " + String(ownBufOffset) + ", OUT: " + String(outBufOffset));
            for (int x = 0; x < renderArea.width; x += 2)
            {
                outBuf[outBufOffset++] = ((ownBuf[ownBufOffset] & 0x0F) << 4) | (ownBuf[ownBufOffset + 1] & 0x0F);
                ownBufOffset += 2;
            }
        }
    };

    /// @brief Convert a 4-bit greyscale value to a 16-bit colour value
    /// Useful for drawing with the TFT_eSprite library functions.
    /// @param grey A four-bit greyscale value. 0 is black, 15 is white.
    /// @return A 16-bit 565 formatted colour value.
    uint16_t greyToColour16(int grey)
    {
        return (grey & 0x03) << 3 | (grey & 0x0C) << 6;
    }

public:
    enum layer_t
    {
        LAYER_BG,
        LAYER_LOWER,
        LAYER_CENTRE,
        LAYER_UPPER,
        LAYER_TOP,
        LAYER_OVERLAY
    };
    int x;
    int y;
    int width;
    int height;
    UiObj *parent = NULL;
    TFT_eSprite surface = NULL;
    bool updated = false;
    bool exposed = false;
    bool hardwareDraw = false;
    bool unbuffered = true;
    bool visible = true;
    bool initialised = false;
    bool visibilityChanged = false;
    bool newParent = false;
    bool drawn = false;
    enum layer_t layer = LAYER_CENTRE;
    struct area updateArea;
    struct area exposeArea;
};

/**
 * @brief A simple label object.
 * @details A simple label object. Can be used to display text on the screen.
 * A number of formatting options are available.
 */

class UiLabel : public UiObj
{
public:
    enum hAlign
    {
        ALIGN_LEFT,
        ALIGN_CENTRE,
        ALIGN_RIGHT
    };

    enum vAlign
    {
        ALIGN_TOP,
        ALIGN_MIDDLE,
        ALIGN_BOTTOM
    };

    UiLabel(int x, int y, int width, int height, String text, bool unbuffered = false) : UiObj()
    {
        this->init(x, y, width, height, text, unbuffered);
    };

    UiLabel(int x, int y, String text) : UiObj()
    {
        this->init(x, y, text);
    };

    void init(int x, int y, String text)
    {
        UiObj::init(x, y, 0, 0);
        this->surface = TFT_eSprite(NULL);
        this->surface.setTextSize(3);
        int width = this->surface.textWidth(text);
        int height = this->surface.fontHeight(1);
        this->init(x, y, 0, 0, text);
        this->autosize = true;
        this->autoResize(true);
    };

    UiLabel() : UiObj()
    {
        this->initialised = false;
    };

    void init(int x, int y, int width, int height, String text, bool unbuffered = false)
    {
        UiObj::init(x, y, width, height, unbuffered);
        this->text = text;
        this->hasOutline = false;
        this->hasFill = false;
        this->textColour = this->greyToColour16(15);
        this->textSize = 3;
        this->customFont = false;
        this->initialised = true;
        this->autosize = false;
        this->resizeNeeded = false;
        this->xPad = 10;
        this->yPad = 10;
        this->setOutline(1, 10);
        this->hasPreRender = false;
        this->updated = true;
        this->vAlignment = ALIGN_MIDDLE;
        this->hAlignment = ALIGN_CENTRE;
        this->lineSpacing = 0.5;
    };

    bool isUpdated() override
    {
        return this->updated;
    };

    struct area getUpdateArea() override
    {
        debug("UiLabel::getUpdateArea()");
        this->updateArea.x = 0;
        this->updateArea.y = 0;
        this->updateArea.width = this->width;
        this->updateArea.height = this->height;
        return this->updateArea;
    };

    void draw() override
    {
        if (!this->initialised)
        {
            return;
        }

        if (this->hasPreRender)
        {
            this->hasPreRender = false;
            return;
        }

        if (this->autosize && this->resizeNeeded)
        {
            if (width == 0 || height == 0)
            {
                this->autoResize(true);
            }
            this->autoResize();
        }
        this->surface.fillSprite(this->greyToColour16(0));
        this->drawFill();
        this->drawText();
        this->drawOutline();
    };

    void drawText()
    {
        struct area textArea;
        if (this->text == "")
        {
            return;
        }

        if (this->customFont)
        {
            surface.setFreeFont(this->font);
        }
        else
        {
            surface.setTextSize(this->textSize);
        }
        surface.setTextColor(this->textColour);
        surface.setTextDatum(TL_DATUM);
        textArea = this->getTextPos();
        if (this->surface.textWidth(this->text) > this->width - this->xPad * 2)
        {
            this->drawMultilineText(textArea);
        }
        else
        {
            surface.drawString(this->text, textArea.x, textArea.y);
        }
    };

    void drawMultilineText(struct area textArea)
    {
        int lineCount = 0;
        int lineStart = 0;
        int lineEnd = 0;
        int lineLength = 0;
        int lineY = 0;
        int lineX = 0;
        int lineHeight = this->surface.fontHeight(1);
        int lineWidth = 0;
        int textAreaWidth = this->width - this->xPad * 2 - this->outlineThickness * 2;

        lineHeight += lineHeight * this->lineSpacing;
        debug("UiLabel::drawMultilineText()");
        debug("textAreaWidth: " + String(textAreaWidth));
        while (lineEnd < this->text.length())
        {
            lineEnd = this->lineLimit(this->text, lineStart);
            textArea.x = this->getTextHPos(this->text.substring(lineStart, lineEnd));
            this->surface.drawString(this->text.substring(lineStart, lineEnd), textArea.x, textArea.y + lineY);
            lineY += lineHeight;
            lineLength = lineEnd - lineStart;
            lineCount++;
            lineStart = lineEnd;
        }
    };

    int lineLimit(String text, int offset)
    {
        int lineStart = offset;
        int lineEnd = text.indexOf('\n');
        int lineWidth = 0;
        int textAreaWidth = this->width - this->xPad * 2 - this->outlineThickness * 2;

        if (lineEnd == -1)
        {
            lineEnd = text.length();
        }

        while (true)
        {
            lineWidth = this->surface.textWidth(text.substring(lineStart, lineEnd));
            if (lineWidth <= textAreaWidth || lineEnd - lineStart <= 1)
            {
                break;
            }
            do
            {
                lineEnd--;
            } while (text.charAt(lineEnd) != ' ' && lineEnd > lineStart);
        }
        return lineEnd;
    };

    int getTextHPos(String text)
    {
        int textWidth = this->surface.textWidth(text);
        int textAreaWidth = this->width - this->xPad * 2 - this->outlineThickness * 2;

        if (textWidth > textAreaWidth)
        {
            return this->xPad + this->outlineThickness;
        }

        if (this->hAlignment == ALIGN_LEFT)
        {
            return this->xPad + this->outlineThickness;
        }
        else if (this->hAlignment == ALIGN_CENTRE)
        {
            return this->width / 2 - textWidth / 2;
        }
        else if (this->hAlignment == ALIGN_RIGHT)
        {
            return this->width - textWidth - this->xPad - this->outlineThickness;
        }
        return 0;
    };

    struct area getTextPos()
    {
        struct area pos;
        int textAreaWidth = this->width - this->xPad * 2 - this->outlineThickness * 2;
        pos.width = this->surface.textWidth(this->text);
        pos.height = this->surface.fontHeight(1);

        if (pos.width > textAreaWidth)
        {
            pos.height = this->getMultilineHeight();
            pos.width = textAreaWidth;
        }
        else
        {
            pos.height = this->surface.fontHeight(1);
        }

        if (this->hAlignment == ALIGN_LEFT)
        {
            pos.x = this->xPad + this->outlineThickness;
        }
        else if (this->hAlignment == ALIGN_CENTRE)
        {
            pos.x = this->width / 2 - pos.width / 2;
        }
        else if (this->hAlignment == ALIGN_RIGHT)
        {
            pos.x = this->width - pos.width - this->xPad - this->outlineThickness;
        }

        if (this->vAlignment == ALIGN_TOP)
        {
            pos.y = this->yPad + this->outlineThickness;
        }
        else if (this->vAlignment == ALIGN_MIDDLE)
        {
            pos.y = this->height / 2 - pos.height / 2 + 1;
        }
        else if (this->vAlignment == ALIGN_BOTTOM)
        {
            pos.y = this->height - pos.height - this->yPad - this->outlineThickness;
        }
        return pos;
    }

    int getMultilineHeight()
    {
        int lineWidth;
        int lineStart = 0;
        int lineEnd;
        int height = 0;
        int fontHeight = this->surface.fontHeight(1);
        int lineHeight = fontHeight + fontHeight * this->lineSpacing;

        while (true)
        {
            if (lineStart >= this->text.length())
            {
                break;
            }
            lineEnd = this->lineLimit(this->text, lineStart);
            height += lineHeight;
            lineStart = lineEnd;
        }
        height -= fontHeight * this->lineSpacing;
        return height;
    };

    void drawFill()
    {
        if (this->hasFill)
        {
            if (this->fillRounded)
            {
                this->surface.fillRoundRect(0, 0, width, height, this->fillRoundingRadius, this->fillColour);
            }
            else
            {
                this->surface.fillRect(0, 0, width, height, this->fillColour);
            }
        }
    };

    void drawOutline()
    {
        if (this->hasOutline)
        {
            for (int i = 0; i < this->outlineThickness; i++)
            {
                if (this->fillRounded)
                {
                    this->surface.drawRoundRect(i, i, width - i * 2, height - i * 2, this->fillRoundingRadius, this->outlineColour);
                }
                else
                {
                    this->surface.drawRect(i, i, width - i * 2, height - i * 2, this->outlineColour);
                }
                this->surface.drawRoundRect(i, i, width - i * 2, height - i * 2, 10, this->outlineColour);
            }
        }
    };

    void setOutline(uint16_t colour, uint16_t thickness, uint16_t roundingRadius = 0)
    {
        if (thickness > 0)
        {
            this->hasOutline = true;
            this->outlineColour = this->greyToColour16(colour);
            this->outlineThickness = thickness;
            if (roundingRadius > 0)
            {
                this->borderRounded = true;
                this->borderRoundingRadius = roundingRadius;
            }
            else
            {
                this->borderRounded = false;
            }
        }
        else
        {
            this->hasOutline = false;
        }
        this->resizeNeeded = true;
        this->updated = true;
    };

    void setTextColour(uint16_t colour)
    {
        this->textColour = this->greyToColour16(colour);
        this->updated = true;
    };

    void setTextSize(int size)
    {
        this->textSize = size;
        this->resizeNeeded = true;
        this->updated = true;
    };

    void setFill(uint16_t colour, uint16_t roundingRadius = 0)
    {
        this->hasFill = true;
        this->fillColour = this->greyToColour16(colour);
        if (roundingRadius > 0)
        {
            this->fillRounded = true;
            this->fillRoundingRadius = roundingRadius;
        }
        else
        {
            this->fillRounded = false;
        }
        this->updated = true;
    };

    void noFill()
    {
        this->hasFill = false;
        this->resizeNeeded = true;
        this->updated = true;
    };

    void noBorder()
    {
        this->hasOutline = false;
        this->resizeNeeded = true;
        this->updated = true;
    };

    void setText(String text)
    {
        this->text = text;
        this->resizeNeeded = true;
        this->updated = true;
    };

    void setFont(GFXfont *font)
    {
        this->customFont = true;
        this->font = font;
        this->resizeNeeded = true;
        this->updated = true;
    };

    void preRender()
    {
        if (!this->initialised)
        {
            return;
        }
        this->draw();
        this->hasPreRender = true;
    };

    void autoResize(bool init = false)
    {
        this->autosize = true;
        this->resizeNeeded = false;
        if (this->customFont)
        {
            surface.setFreeFont(this->font);
        }
        else
        {
            surface.setTextSize(this->textSize);
        }
        width = this->surface.textWidth(this->text) + this->xPad * 2 + this->outlineThickness * 2;
        height = this->surface.fontHeight(1) + this->yPad * 2 + this->outlineThickness * 2;

        if (init)
        {
            this->createBuffer(width, height);
        }
        else
        {
            this->resize(width, height);
        }
        this->updated = true;
    };

    void resize(int width, int height)
    {
        this->width = width;
        this->height = height;
        this->surface.deleteSprite();
        this->surface.createSprite(width, height, 1);
        this->updated = true;
    };

    void defaultFont()
    {
        this->customFont = false;
        this->resizeNeeded = true;
        this->updated = true;
    };

    void setPadding(int xPad, int yPad)
    {
        this->xPad = xPad;
        this->yPad = yPad;
        this->resizeNeeded = true;
    };

    bool touchEvent(int x, int y) override
    {
        return false;
    }

public:
    String text;
    bool initialised;
    bool hasOutline;
    bool hasFill;
    bool borderRounded;
    bool fillRounded;
    bool customFont;
    bool autosize;
    bool resizeNeeded;
    uint16_t borderRoundingRadius;
    uint16_t fillRoundingRadius;
    uint16_t outlineColour;
    uint16_t outlineThickness;
    uint16_t fillColour;
    uint16_t textColour;
    uint16_t textSize;
    uint16_t textAlignment;
    uint16_t xPad;
    uint16_t yPad;
    GFXfont *font;
    bool hasPreRender;
    enum vAlign vAlignment;
    enum hAlign hAlignment;
    float lineSpacing;
};

class UiFrame : public UiLabel
{
public:
    UiFrame(int x, int y, int width, int height, bool hwFrame = false, M5EPD_Canvas *hwSurface = NULL) : UiLabel()
    {
        UiFrame::init(x, y, width, height, hwFrame, hwSurface);
    };

    UiFrame() : UiLabel()
    {
        this->initialised = false;
    };

    void init(int x, int y, int width, int height, bool hwFrame = false, M5EPD_Canvas *hwSurface = NULL)
    {
        this->initialised = true;
        this->hwFrame = hwFrame;
        this->hwSurface = hwSurface;
        if (!hwFrame)
        {
            UiLabel::init(x, y, width, height, "", false);
        }
        else
        {
            UiLabel::init(x, y, width, height, "", true);
        }
        this->hardwareDraw = hwFrame;
        this->backgroundColour = this->greyToColour16(1);
        this->setOutline(10, 2, 10);
        this->fillRoundingRadius = 10;
    };

    void add(UiObj *obj)
    {
        this->objects.push_back(obj);
        obj->setParent(this);
        this->itemsChanged = true;
    };

    void resetStatus() override
    {
        if (this->drawn)
        {
            this->itemsChanged = false;
        }
        UiObj::resetStatus();
        for (UiObj *obj : this->objects)
        {
            obj->resetStatus();
        }
    };

    bool isUpdated() override
    {
        if (!this->initialised)
        {
            debug("Frame not updated (uninitialised)");
            return false;
        }

        if (this->itemsChanged || this->visibilityChanged || this->newParent)
        {
            // debug("Frame updated");
            return true;
        }

        for (UiObj *obj : this->objects)
        {
            if (this->isObjectChanged(obj))
            {
                // debug("Frame updated");
                return true;
            }
        }
        // debug("Frame not updated");
        return false;
    };

    bool isObjectChanged(UiObj *obj)
    {
        if (obj->initialised && ((obj->isUpdated() && obj->visible) || obj->visibilityChanged || obj->newParent))
        {
            return true;
        }
        return false;
    };

    bool withinArea(struct area checkArea, UiObj *obj)
    {
        if (obj->x >= checkArea.x && obj->x + obj->width <= checkArea.x + checkArea.width && obj->y >= checkArea.y && obj->y + obj->height <= checkArea.y + checkArea.height)
        {
            return true;
        }
        return false;
    };

    bool overlaps(UiObj *obj1, UiObj *obj2)
    {
        if (obj1->x + obj1->width < obj2->x || obj1->x > obj2->x + obj2->width || obj1->y + obj1->height < obj2->y || obj1->y > obj2->y + obj2->height)
        {
            return false;
        }
        return true;
    };

    void updateBelow(UiObj *obj)
    {
        for (UiObj *child : this->objects)
        {
            if (this->overlaps(obj, child))
            {
                child->visibilityChanged = true;
            }
        }
    };

    struct area getUpdateArea()
    {
        debug("Getting frame update area");
        if (!this->initialised)
        {
            return {0, 0, 0, 0};
        }

        if (!isUpdated())
        {
            this->updateArea = {0, 0, 0, 0};
            return this->updateArea;
        }

        this->updateArea = {-1, -1, 0, 0};

        for (UiObj *obj : this->objects)
        {
            if (obj->initialised && obj->visibilityChanged && !obj->visible)
            {
                this->updateBelow(obj);
            }
        }

        if (this->visibilityChanged)
        {
            this->updateArea = {0, 0, this->width, this->height};
            return this->updateArea;
        }

        for (UiObj *obj : this->objects)
        {
            // debug("Object position: [" + String(obj->x) + ", " + String(obj->y) + ", " + String(obj->width) + ", " + String(obj->height) + "]");
            // debug("Object status: [" + String(obj->initialised) + ", " + String(obj->isUpdated()) + ", " + String(obj->visible) + ", " + String(obj->visibilityChanged) + ", " + String(obj->newParent) + "]");
            if (isObjectChanged(obj))
            {
                if (this->updateArea.x == -1)
                {
                    this->updateArea.x = obj->x;
                }
                else
                {
                    this->updateArea.x = min(this->updateArea.x, obj->x);
                }

                if (this->updateArea.y == -1)
                {
                    this->updateArea.y = obj->y;
                }
                else
                {
                    this->updateArea.y = min(this->updateArea.y, obj->y);
                }

                this->updateArea.width = max(this->updateArea.width, obj->width + obj->x - this->updateArea.x);
                this->updateArea.height = max(this->updateArea.height, obj->height + obj->y - this->updateArea.y);
            }
        }
        if (this->updateArea.x < 0 || this->updateArea.y < 0)
        {
            this->updateArea.x = 0;
            this->updateArea.y = 0;
        }
        debug("Frame size: " + String(this->width) + ", " + String(this->height));

        if (this->hwFrame)
        {
            // A hardware frame must be a multiple of 4 pixels wide.
            this->updateArea.width = ((this->updateArea.width + 3) >> 2) << 2;
        }
        debug("Frame update area: " + String(this->updateArea.x) + ", " + String(this->updateArea.y) + ", " + String(this->updateArea.width) + ", " + String(this->updateArea.height));
        return this->updateArea;
    }

    void draw()
    {
        if (!this->initialised)
        {
            return;
        }

        if (this->hwFrame)
        {
            debug("Drawing frame (H/W)");
        }
        else
        {
            debug("Drawing frame (S/W)");
        }
        this->getUpdateArea();
        if (visibilityChanged)
        {
            this->surface.fillSprite(this->backgroundColour);
        }
        UiLabel::draw();
        for (int layer = 0; layer <= UiObj::LAYER_OVERLAY; layer++)
        {
            // debug("Drawing layer " + String(layer));
            for (UiObj *obj : this->objects)
            {
                if (obj->visible && obj->layer == layer)
                { // && this->isObjectChanged(obj)) {
                    // debug("Drawing object from parent: " + String((uint32_t)obj->parent));
                    obj->render();
                }
            }
        }
        this->drawOutline();
        debug("Done.");
    };

    void expose(struct area xArea) override
    {
        if (this->hwFrame || this->parent == NULL)
        {
            for (UiObj *obj : this->objects)
            {
                if (obj->visible && this->withinArea(xArea, obj))
                {
                    obj->expose(obj->childOffset(xArea));
                }
            }
        }
        else
        {
            UiObj::expose(xArea);
        }
    };

    bool touchEvent(int x, int y)
    {
        if (!this->initialised)
        {
            return false;
        }

        debug("Frame touch event: " + String(x) + ", " + String(y));
        for (int layer = UiObj::LAYER_OVERLAY; layer >= 0; layer--)
        {
            for (UiObj *obj : this->objects)
            {
                if (obj->layer == layer && obj->visible && obj->layer == layer && x >= obj->x && x <= obj->x + obj->width && y >= obj->y && y <= obj->y + obj->height)
                {
                    return obj->touchEvent(x - obj->x, y - obj->y);
                }
            }
        }
        debug("Not matching any object");
        return false;
    };

public:
    std::vector<UiObj *> objects;
    bool initialised;
    bool hwFrame;
    bool itemsChanged;
    M5EPD_Canvas *hwSurface;
    uint16_t backgroundColour;
    uint16_t borderColour;
};

/**
 * @brief A button control with a callback function.
 * @details This class is a button control with a callback function.
 * It inherits from UiLabel, so all formatting is available.
 */
class UiButton : public UiLabel
{
public:
    enum buttonEvent
    {
        BUTTON_PRESSED = 100,
        BUTTON_RELEASED,
        BUTTON_HOLD,
        BUTTON_DOUBLE_TAP,
    };

    UiButton(int x, int y, String text, bool (*callback)(UiButton *, int)) : UiLabel(x, y, text)
    {
        this->useStdFunction = false;
        this->callback = callback;
        this->setOutline(15, 5, 5);
        this->setFill(2, 5);
    };

    UiButton(int x, int y, String text, std::function<bool(UiButton *, int)> callback) : UiLabel(x, y, text)
    {
        this->useStdFunction = true;
        this->stdCallback = callback;
        this->setOutline(15, 5, 5);
        this->setFill(2, 5);
    };

    void init(int x, int y, String text, bool (*callback)(UiButton *, int))
    {
        UiLabel::init(x, y, text);
        this->callback = callback;
    };

    void setOutline(uint16_t colour, uint16_t thickness, uint16_t roundingRadius)
    {
        this->hasOutline = true;
        this->borderColour = colour;
        this->outlineThickness = thickness;
        this->borderRoundingRadius = roundingRadius;
        this->updated = true;
    };

    UiButton() : UiLabel()
    {
        this->callback = NULL;
    };

    bool touchEvent(int x, int y) override
    {
        if (!this->initialised || this->callback == NULL)
        {
            return false;
        }
        else
        {
            if (this->useStdFunction)
            {
                return this->stdCallback(this, BUTTON_RELEASED);
            }
            else
            {
                return this->callback(this, BUTTON_RELEASED);
            }
        }
    };

public:
    bool (*callback)(UiButton *, int);
    std::function<bool(UiButton *, int)> stdCallback;
    int borderColour;

private:
    bool useStdFunction;
};

class UiImage : public UiObj
{
public:
    UiImage(int x, int y, int width, int height, uint8_t *bitmap) : UiObj(x, y, width, height)
    {
        this->image = bitmap;
        this->initialised = true;
        this->backgroundColour = this->greyToColour16(0);
        this->updated = true;
    };

    UiImage() : UiObj()
    {
        this->initialised = false;
    };

    bool isUpdated() override
    {
        return this->updated;
    };

    struct area getUpdateArea() override
    {
        this->updateArea.x = this->x;
        this->updateArea.y = this->y;
        this->updateArea.width = this->width;
        this->updateArea.height = this->height;
        return this->updateArea;
    };

    void draw() override
    {
        if (!this->initialised)
        {
            return;
        }
        this->surface.fillScreen(this->backgroundColour);
        Serial.println("Frame buffer: " + String((uint32_t)this->surface.frameBuffer(1)));
        this->surface.drawBitmap(0, 0, this->image, this->width, this->height, this->greyToColour16(15));
    };

    bool touchEvent(int x, int y) override
    {
        return false;
    };

public:
    uint8_t *image;
    bool initialised;
    uint16_t backgroundColour;
};

class UiTextBox
{
    // TODO
};

class UiIcon : public UiFrame
{
public:
    UiIcon(int x, int y, int width, int height, uint8_t *bitmap, String text, bool (*callback)(UiObj *, int)) : UiFrame(x, y, width, height)
    {
        this->image = new UiImage(0, 0, width, height, bitmap);
        this->label = new UiLabel(0, height, text);
        this->callback = callback;
        this->add(image);
        this->add(label);
    };

    UiIcon() : UiFrame()
    {
        this->image = NULL;
        this->label = NULL;
    };

    bool touchEvent(int x, int y) override
    {
        if (!this->initialised || this->callback == NULL)
        {
            return false;
        }
        else
        {
            return this->callback(this, 1);
        }
    };

public:
    UiLabel *label;
    UiImage *image;
    bool (*callback)(UiObj *, int);
};

class UiHwImage : public UiObj
{
public:
    UiHwImage(int x, int y, int width, int height, String filename, M5EPD_Canvas *surface) : UiObj()
    {
        if (!SD.exists(filename))
        {
            Serial.println("File not found: " + filename);
            this->initialised = false;
            return;
        }
        File imgFile = SD.open(filename, FILE_READ);
        if (!imgFile)
        {
            Serial.println("Failed to open file: " + filename);
            this->initialised = false;
            return;
        }

        this->imgBuffer = (uint8_t *)calloc(imgFile.size(), 1);
        imgFile.read(this->imgBuffer, imgFile.size());

        this->x = x;
        this->y = y;
        this->width = width;
        this->height = height;
        this->filename = filename;
        this->initialised = true;
        this->hardwareDraw = true;
        this->hwsurface = surface;
        SD.open(filename, FILE_READ);
    };

    UiHwImage() : UiObj()
    {
        this->initialised = false;
    };

    bool isUpdated() override
    {
        return false;
    };

    struct area getUpdateArea() override
    {
        return {0, 0, 0, 0};
    };

    void draw()
    {
        if (!this->initialised)
        {
            return;
        }

        String fileExt = this->getFileExtension(this->filename);
        bool isUrl = this->isWebUrl(this->filename);

        if (isUrl)
        {
            if (fileExt == "png")
            {
                this->hwsurface->drawPngUrl(this->filename.c_str(), this->x, this->y, this->width, this->height);
            }
            else if (fileExt == "jpg" || fileExt == "jpeg")
            {
                this->hwsurface->drawJpgUrl(this->filename.c_str(), this->x, this->y, this->width, this->height);
            }
            else
            {
                Serial.println("Unsupported file extension: " + fileExt);
            }
        }
        else
        {
            if (fileExt == "png")
            {
                this->hwsurface->drawPngFile(SD, this->filename.c_str(), this->x, this->y, this->width, this->height);
            }
            else if (fileExt == "jpg" || fileExt == "jpeg")
            {
                this->hwsurface->drawJpgFile(SD, this->filename.c_str(), this->x, this->y, this->width, this->height);
            }
            else if (fileExt == "bmp")
            {
                this->hwsurface->drawBmpFile(SD, this->filename.c_str(), this->x, this->y);
            }
            else
            {
                Serial.println("Unsupported file extension: " + fileExt);
            }
        }
    };

    String getFileExtension(String filename)
    {
        int dotIndex = filename.lastIndexOf('.');
        if (dotIndex == -1)
        {
            return "";
        }
        else
        {
            return filename.substring(dotIndex + 1);
        }
    };

    bool isWebUrl(String filename)
    {
        if (filename.startsWith("http://") || filename.startsWith("https://"))
        {
            return true;
        }
        else
        {
            return false;
        }
    };

    bool touchEvent(int x, int y) override
    {
        return false;
    };

public:
    bool initialised;
    String filename;
    uint16_t backgroundColour;
    M5EPD_Canvas *hwsurface;
    uint8_t *imgBuffer;
};

class UiModal : public UiFrame
{
public:
    UiModal() : UiFrame()
    {
        UiFrame::init(50, 200, 400, 300);
        this->createElements();
        this->result = -1;
        this->initialised = true;
        this->hide();
        this->layer = LAYER_OVERLAY;
    };

    UiModal(int x, int y, int width, int height) : UiFrame(x, y, width, height)
    {
        this->createElements();
        this->initialised = true;
        this->result = -1;
        this->hide();
    };

    void msgbox(String title, String message)
    {
        debug("Modal object: " + String((uint32_t)this));
        this->titlebar->setText(title);
        this->content->setText(message);
        this->cancelButton->hide();
        this->okButton->show();
        this->okButton->move((this->width - this->okButton->width - 10) / 2, this->height - this->okButton->height - 10);
        this->result = -1;
        this->show();
    };

    void confirm(String title, String message)
    {
        debug("Modal object: " + String((uint32_t)this));
        this->titlebar->setText(title);
        this->content->setText(message);
        this->cancelButton->show();
        this->okButton->show();
        this->cancelButton->move(this->width / 2, this->height - this->okButton->height - 10);
        this->okButton->move(this->width / 4, this->height - this->okButton->height - 10);
        this->result = -1;
        this->show();
    };

private:
    void createElements()
    {
        this->setOutline(15, 4, 10);
        this->titlebar = new UiLabel(0, 0, this->width, 30, "");
        this->titlebar->setOutline(15, 4, 10);
        this->closeButtonCallback = std::bind(&UiModal::closePressed, this, std::placeholders::_1, std::placeholders::_2);
        this->okButtonCallback = std::bind(&UiModal::okPressed, this, std::placeholders::_1, std::placeholders::_2);
        this->cancelButtonCallback = std::bind(&UiModal::cancelPressed, this, std::placeholders::_1, std::placeholders::_2);
        this->closeButton = new UiButton(this->width - 50, 0, "X", closeButtonCallback);
        this->okButton = new UiButton(100, this->height - 100, "OK", okButtonCallback);
        this->cancelButton = new UiButton(this->width - 100, this->height - 100, "Cancel", cancelButtonCallback);
        this->content = new UiLabel(0, 30, this->width, this->height - 30 - this->okButton->height, "");
        this->content->noBorder();
        this->add(this->closeButton);
        this->add(this->titlebar);
        this->add(this->content);
        this->add(this->okButton);
        this->add(this->cancelButton);
        this->cancelButton->preRender();
        this->okButton->preRender();
    }

    bool closePressed(UiObj *obj, int event)
    {
        this->result = 0;
        this->hide();
        return true;
    };

    bool okPressed(UiObj *obj, int event)
    {
        this->result = 0;
        this->hide();
        return true;
    };

    bool cancelPressed(UiObj *obj, int event)
    {
        this->result = 1;
        this->hide();
        return true;
    };

public:
    int result;
    bool initialised;

private:
    UiLabel *titlebar;
    UiLabel *content;
    UiButton *closeButton;
    UiButton *okButton;
    UiButton *cancelButton;
    std::function<bool(UiObj *, int)> closeButtonCallback;
    std::function<bool(UiObj *, int)> okButtonCallback;
    std::function<bool(UiObj *, int)> cancelButtonCallback;
};

class UiManager : public UiFrame
{
public:
    UiManager(M5EPD_Canvas *parentSurface) : UiFrame()
    {
        this->parentSurface = parentSurface;
        UiFrame::init(0, 0, parentSurface->width(), parentSurface->height(), true, parentSurface);
        this->setOutline(0, 0);
        this->modal = NULL;
        this->addModal();
        this->lastDisplayUpdate = 0;
    };

    void addModal(UiModal *modal = NULL)
    {
        if (this->modal != NULL)
        {
            return;
        }
        if (modal == NULL)
        {
            modal = new UiModal();
        }
        this->modal = modal;
        this->add(modal);
    };

    void msgbox(String title, String message)
    {
        this->modal->msgbox(title, message);
    };

    void confirm(String title, String message)
    {
        this->modal->confirm(title, message);
    };

    void updateDisplay()
    {
        this->getUpdateArea();
        this->render();
        if (updateArea.height == 0 || updateArea.width == 0)
        {
            debug("No update area, skipping update.");
            return;
        }
        debug("Writing PARTGRAM4pp to display area: " + String(this->updateArea.x) + ", " + String(this->updateArea.y) + ", " + String(this->updateArea.width) + ", " + String(this->updateArea.height));
        M5.EPD.WritePartGram4bpp(this->updateArea.x, this->updateArea.y, this->updateArea.width, this->updateArea.height, screenBuffer);
        debug("Running partial refresh on area " + String(this->updateArea.x) + ", " + String(this->updateArea.y) + ", " + String(this->updateArea.width) + ", " + String(this->updateArea.height));
        M5.EPD.UpdateArea(updateArea.x, updateArea.y, updateArea.width, updateArea.height, UPDATE_MODE_INIT);
        delay(100);
        M5.EPD.UpdateArea(this->updateArea.x, this->updateArea.y, this->updateArea.width, this->updateArea.height, UPDATE_MODE_GC16);
        debug("Partial refresh complete");
        this->lastDisplayUpdate = micros();
    };

    bool touchEvent(int x, int y) override
    {
        if (this->modal != NULL && this->modal->visible)
        {
            if (x > this->modal->x && x < this->modal->x + this->modal->width && y > this->modal->y && y < this->modal->y + this->modal->height)
            {
                return this->modal->touchEvent(x - this->modal->x, y - this->modal->y);
            }
            else
            {
                return false;
            }
        }
        else
        {
            return UiFrame::touchEvent(x, y);
        }
    };

    void sleepUntilTouch()
    {
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, LOW);
        M5.disableEXTPower();
        // M5.disableEPDPower();
        WiFi.setSleep(WIFI_PS_NONE);
        esp_wifi_stop();
        if (micros() - lastDisplayUpdate < 500000)
        {
            debug("Waiting for EPD to draw. Sleeping for 500ms");
            delay(500);
        }
        M5.disableEPDPower();
        gpio_hold_en((gpio_num_t)M5EPD_MAIN_PWR_PIN);
        esp_light_sleep_start();
        M5.enableEPDPower();
        delay(10);
        debug("Woken up");
    };

public:
    UiModal *modal = NULL;
    unsigned long lastDisplayUpdate = 0;

private:
    M5EPD_Canvas *parentSurface = NULL;
};

M5EPD_Canvas *canvas = nullptr;
UiManager *mainUi = nullptr;
UiLabel *quotes = nullptr;

bool buttonCallback(UiButton *btn, int event)
{
    debug("Button pressed");
    mainUi->msgbox("Hello", "Message here!");
    debug("Message box closed");
    return true;
}

bool button2Callback(UiButton *btn, int event)
{
    debug("Button 2 pressed");
    mainUi->confirm("Facts", "The penumbra is the lighter outer part of a shadow.");
    debug("Confirm box closed");
    return true;
}

bool refreshCallback(UiButton *btn, int event)
{
    btn->parent->visibilityChanged = true;
    mainUi->updateDisplay();
    return true;
}

const String randomQuote()
{
    int index = random(sizeof(progQuotes) / sizeof(String));
    debug("Random quote index: " + String(index));
    debug("Random quote: " + progQuotes[index]);
    return progQuotes[index];
}

bool setQuote(UiButton *btn, int event)
{
    if (event == UiButton::BUTTON_RELEASED)
    {
        quotes->setText(randomQuote());
        return true;
    }
    return false;
}

void setup()
{
    screenBuffer = (uint8_t *)calloc(540, 960 / 2);
    M5.begin();
    M5.EPD.SetRotation(90);
    M5.EPD.Clear(true);
    M5.TP.SetRotation(90);
    M5.RTC.begin();
    canvas = new M5EPD_Canvas(&M5.EPD);
    canvas->createCanvas(540, 960);
    mainUi = new UiManager(canvas);

    UiImage *bg = new UiImage(0, 0, 540, 960, (uint8_t *)epd_bitmap_frame_2);
    mainUi->add(bg);
    bg->layer = UiObj::LAYER_BG;
    quotes = new UiLabel(60, 150, 410, 560, "");
    quotes->layer = UiObj::LAYER_LOWER;
    quotes->setText(randomQuote());
    UiButton *nextQuoteBtn = new UiButton(175, 730, "Next Quote", setQuote);
    mainUi->add(quotes);
    mainUi->add(nextQuoteBtn);
    // UiLabel *label1 = new UiLabel(100, 170, "This is a label.");
    // mainUi->add((UiObj*) label1);
    // label1->setOutline(15, 5, 20);
    // label1->setFill(5, 20);
    // UiLabel *label2 = new UiLabel(100, 300, "Another label...");
    // mainUi->add((UiObj*) label2);
    // label2->setOutline(5, 20, 3);
    //
    // UiFrame *frame = new UiFrame(120, 400, 350, 300);
    // UiLabel *btn1 = new UiButton(20, 20, "Dialog Box", buttonCallback);
    // UiLabel *btn2 = new UiButton(20, 100, "Interesting Fact", button2Callback);
    // UiLabel *btn3 = new UiButton(20, 200, "Refresh Frame", refreshCallback);
    // frame->add(btn1);
    // frame->add(btn2);
    // frame->add(btn3);
    // mainUi->add((UiObj*) frame);
    mainUi->updateDisplay();
    // label1->setText("Label updated!");
}

void powerOff()
{
    M5.EPD.Clear(true);
    M5.shutdown();
}

void processEvents(UiManager *ui)
{
    static bool fingerDown = false;
    static tp_finger_t tp;
    static tp_finger_t lastTp;
    static unsigned long touchStart = 0;
    static unsigned long touchEnd = 0;

    M5.update();
    ui->resetStatus();
    while (M5.TP.avaliable())
    {
        if (M5.TP.isFingerUp())
        {
            if (fingerDown)
            {
                fingerDown = false;
                if (micros() - touchEnd < 500000)
                {
                    debug("Ignoring touch event");
                    continue;
                }
                debug("Touch event at " + String(tp.x) + ", " + String(tp.y));
                if (ui->touchEvent(tp.x, tp.y))
                {
                    ui->updateDisplay();
                }
                touchEnd = micros();
                lastTp.x = tp.x;
                lastTp.y = tp.y;
            }
        }
        else
        {
            M5.TP.update();
            tp = M5.TP.readFinger(0);
            if (tp.x == lastTp.x && tp.y == lastTp.y)
            {
                continue;
            }
            fingerDown = true;
            touchStart = micros();
        }
    }

    M5.update();
    if (M5.BtnP.wasPressed())
    {
        powerOff();
    }
    else
    {
        ui->sleepUntilTouch();
    }
}

void loop() {
    processEvents(mainUi);
}
