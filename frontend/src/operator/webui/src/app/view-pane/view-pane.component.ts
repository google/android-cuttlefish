import {
  Component,
  OnInit,
  OnDestroy,
  AfterViewInit,
  ViewChild,
  Inject,
  ElementRef,
} from '@angular/core';
import {DisplaysService} from '../displays.service';
import {
  asyncScheduler,
  BehaviorSubject,
  merge,
  fromEvent,
  Observable,
  Subscription,
} from 'rxjs';
import {map, debounceTime, scan} from 'rxjs/operators';
import {
  KtdGridComponent,
  KtdGridLayout,
  KtdGridLayoutItem,
  ktdTrackById,
} from '@katoid/angular-grid-layout';
import {DOCUMENT} from '@angular/common';
import {DisplayInfo} from '../../../../intercept/js/server_connector'

interface DeviceGridItem extends KtdGridLayoutItem {
  id: string;
  x: number;
  y: number;
  w: number;
  h: number;
  display_width: number | null;
  display_height: number | null;
  display_count: number;
  zoom: number;
  visible: boolean;
  placed: boolean;
  [key: string]: string | number | boolean | undefined | null;
}

interface DeviceGridItemUpdate {
  values: DeviceGridItem;
  overwrites: string[];
  source: string;
}

@Component({
  selector: 'app-view-pane',
  templateUrl: './view-pane.component.html',
  styleUrls: ['./view-pane.component.scss'],
})
export class ViewPaneComponent implements OnInit, OnDestroy, AfterViewInit {
  @ViewChild(KtdGridComponent, {static: true}) grid!: KtdGridComponent;
  @ViewChild('viewPane', {static: true}) viewPane!: ElementRef;

  resizeSubscription!: Subscription;
  resizeObserver!: ResizeObserver;

  cols$ = new BehaviorSubject<number>(0);
  cols = 0;

  layoutUpdated$ = new BehaviorSubject<KtdGridLayout>([]);

  trackById = ktdTrackById;

  private readonly minPanelWidth = 330;
  private readonly minPanelHeight = 100;
  private readonly defaultDisplayWidth = 720;
  private readonly defaultDisplayHeight = 1280;
  private readonly defaultDisplayZoom = 0.5;

  // 10px (20 for left+right, 20 for top+bottom) on all four sides of each
  // display.
  private readonly displayMargin = 20;

  // Does not include vertical margins of display device (displayMargin)
  private readonly panelTitleHeight = 53;
  private readonly displayTitleHeight = 48;

  // Note this is constant because displays appear on a single horizontal row.
  private readonly totalVerticalSpacing
      = this.panelTitleHeight
      + this.displayTitleHeight
      + this.displayMargin;

  private totalHorizontalSpacing(item: DeviceGridItem): number {
    const iconPanelWidth = 58;
    const cnt = item.display_count || 1;

    // Separate displays are shown in a row left-to-right, so each new devices
    // adds more margin space.
    // Note we assume control-panel-custom-buttons are not visible. The risk is
    // that they really are, in which case the zoom will be over-calculated and
    // extra vertical space will appear below the displays. Ideally the device
    // would report how much spacing is required in each direction.
    return cnt * this.displayMargin + iconPanelWidth;
  }

  private readonly freeScale = 0;

  constructor(
    public displaysService: DisplaysService,
    @Inject(DOCUMENT) public document: Document
  ) {}

  ngOnInit(): void {
    this.resizeObserver = new ResizeObserver(entries => {
      this.cols$.next(entries[0].contentRect.width);
    });
    this.resizeObserver.observe(this.viewPane.nativeElement);
    this.cols$.subscribe(cols => {
      this.cols = cols;
    });
  }

  ngAfterViewInit(): void {
    this.resizeSubscription = merge(
      fromEvent(window, 'resize'),
      fromEvent(window, 'orientationchange')
    )
      .pipe(debounceTime(50))
      .subscribe(() => {
        this.grid.resize();
      });
  }

  ngOnDestroy(): void {
    this.resizeSubscription.unsubscribe();
    this.resizeObserver.unobserve(this.viewPane.nativeElement);
  }

  private visibleDevicesChanged: Observable<DeviceGridItemUpdate[]> =
    this.displaysService.getDeviceVisibilities().pipe(
      map(deviceVisibilityInfos =>
        deviceVisibilityInfos.map(info => {
          return {
            values: {
              ...this.createDeviceGridItem(info.id),
              visible: info.visible,
            },
            overwrites: ['visible'],
            source: this.visibleDeviceSource,
          };
        })
      )
    );

  private displayInfoChanged: Observable<DeviceGridItemUpdate[]> =
    this.displaysService.getDisplayInfoChanged().pipe(
      map(displayInfo => {
        const updateValues = this.createDeviceGridItem(displayInfo.device_id);
        const overwrites = [];

        if (displayInfo.displays.length !== 0) {
          let w = 0, h = 0;
          displayInfo.displays.forEach((d : DisplayInfo)=> {
            w += d.width;
            h = Math.max(d.height, h);
          });

          updateValues.display_width = w;
          updateValues.display_height = h;

          overwrites.push('display_width');
          overwrites.push('display_height');

          // Display service occasionally sends a dummy update - do not
          // overwrite display count in that case.
          if (displayInfo.displays[0].width !== 0) {
            updateValues.display_count = displayInfo.displays.length;
            overwrites.push('display_count');
          }
        }

        return [
          {
            values: updateValues,
            overwrites: overwrites,
            source: this.displayInfoSource,
          },
        ];
      })
    );

  private layoutUpdated: Observable<DeviceGridItemUpdate[]> =
    this.layoutUpdated$.pipe(
      map(newLayout => {
        return newLayout.map(layoutItem => {
          const updateValues = this.createDeviceGridItem(layoutItem.id);
          const overwrites = ['x', 'y', 'w', 'h'];

          updateValues.x = layoutItem.x;
          updateValues.y = layoutItem.y;
          updateValues.w = layoutItem.w;
          updateValues.h = layoutItem.h;

          return {
            values: updateValues,
            overwrites: overwrites,
            source: this.layoutUpdateSource,
          };
        });
      })
    );

  private adjustNewLayout(item: DeviceGridItem): DeviceGridItem {
    if (item.display_width === null || item.display_height === null)
      return item;

    if (
      item.display_width === this.freeScale ||
      item.display_height === this.freeScale
    )
      return item;

    const zoom = Math.min(
      (item.w - this.totalHorizontalSpacing(item)) / item.display_width,
      (item.h - this.totalVerticalSpacing) / item.display_height
    );

    item.w = Math.max(
      this.minPanelWidth,
      zoom * item.display_width + this.totalHorizontalSpacing(item)
    );
    item.h = Math.max(
      this.minPanelHeight,
      zoom * item.display_height + this.totalVerticalSpacing
    );
    item.zoom = zoom;

    return item;
  }

  private adjustDisplayInfo(item: DeviceGridItem): DeviceGridItem {
    if (item.display_width === null || item.display_height === null)
      return item;

    if (
      item.display_width === this.freeScale ||
      item.display_height === this.freeScale
    ) {
      item.w = this.defaultDisplayZoom * this.defaultDisplayWidth;
      item.h = this.defaultDisplayZoom * this.defaultDisplayHeight;
    } else {
      const zoom = item.zoom;

      item.w = Math.max(
        this.minPanelWidth,
        zoom * item.display_width + this.totalHorizontalSpacing(item)
      );
      item.h = Math.max(
        this.minPanelHeight,
        zoom * item.display_height + this.totalVerticalSpacing
      );
    }

    return item;
  }

  private placeNewItem(
    item: DeviceGridItem,
    layout: DeviceGridItem[]
  ): DeviceGridItem {
    const lastMaxY =
      layout.length !== 0 ? Math.max(...layout.map(obj => obj.y)) : 0;
    const lastRowLayout = layout.filter(obj => obj.y === lastMaxY);
    const lastMaxX =
      layout.length !== 0
        ? Math.max(...lastRowLayout.map(obj => obj.x))
        : -item.w;

    item.x = lastMaxX + item.w * 2 <= this.cols ? lastMaxX + item.w : 0;
    item.y = lastMaxX + item.h * 2 <= this.cols ? lastMaxY : lastMaxY + item.h;

    item.placed = true;

    asyncScheduler.schedule(() => {
      this.forceShowDevice(item.id);
    }, 10000);

    return item;
  }

  resultLayout: Observable<DeviceGridItem[]> = merge(
    this.visibleDevicesChanged,
    this.displayInfoChanged,
    this.layoutUpdated
  ).pipe(
    scan(
      (
        currentLayout: DeviceGridItem[],
        updateInfos: DeviceGridItemUpdate[]
      ) => {
        const layoutById = new Map(currentLayout.map(item => [item.id, item]));

        updateInfos.forEach(updateItem => {
          const id = updateItem.values.id;
          let item = layoutById.has(id)
            ? layoutById.get(id)!
            : updateItem.values;

          updateItem.overwrites.forEach(prop => {
            // Ignore force show display message when display size is already set
            if ((prop === 'display_width' || prop === 'display_height')
               && item[prop] !== null && item[prop] !== this.freeScale
               && updateItem.values[prop] === this.freeScale) {
                 return;
            }

            item[prop] = updateItem.values[prop]!;
          });

          switch (updateItem.source) {
            case this.visibleDeviceSource: {
              if (!item.placed && item.visible) {
                // When a new item is set visible
                item = this.placeNewItem(item, currentLayout);
              }
              break;
            }
            case this.layoutUpdateSource: {
              // When layout is changed
              item = this.adjustNewLayout(item);
              break;
            }
            case this.displayInfoSource: {
              // When device display info is changed
              item = this.adjustDisplayInfo(item);
              break;
            }
          }

          layoutById.set(id, item);
        });

        return Array.from(layoutById, ([, value]) => value);
      },
      []
    ),
    map(items => items.filter(item => item.visible))
  );

  forceShowDevice(deviceId: string) {
    this.displaysService.onDeviceDisplayInfo({
      device_id: deviceId,
      rotation: 0,
      displays: [
        {
          display_id: '0',
          width: this.freeScale,
          height: this.freeScale,
        } as DisplayInfo,
      ],
    });
  }

  private readonly visibleDeviceSource = 'visible_device';
  private readonly displayInfoSource = 'display_info';
  private readonly layoutUpdateSource = 'layout_update';

  private createDeviceGridItem(id: string): DeviceGridItem {
    return {
      id: id,
      x: 0,
      y: 0,
      w: this.minPanelWidth,
      h: this.minPanelHeight,
      display_width: null,
      display_height: null,
      display_count: 0,
      zoom: this.defaultDisplayZoom,
      visible: false,
      placed: false,
    };
  }
}
