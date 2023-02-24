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

interface DeviceGridItem extends KtdGridLayoutItem {
  id: string;
  x: number;
  y: number;
  w: number;
  h: number;
  display_width: number;
  display_height: number;
  zoom: number;
  visible: boolean;
  placed: boolean;
  [key: string]: string | number | boolean | undefined;
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

  readonly minPanelWidth = 330;
  readonly minPanelHeight = 40;

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
          updateValues.display_width = displayInfo.displays[0].width;
          updateValues.display_height = displayInfo.displays[0].height;

          overwrites.push('display_width');
          overwrites.push('display_height');
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
    if (item.display_width === 0 || item.display_height === 0) return item;

    const zoom = Math.min(
      (item.w - 58) / item.display_width,
      (item.h - 40) / item.display_height
    );

    item.w = Math.max(this.minPanelWidth, zoom * item.display_width + 58);
    item.h = Math.max(this.minPanelHeight, zoom * item.display_height + 40);
    item.zoom = zoom;

    return item;
  }

  private adjustDisplayInfo(item: DeviceGridItem): DeviceGridItem {
    const zoom = item.zoom;

    item.w = Math.max(this.minPanelWidth, zoom * item.display_width + 58);
    item.h = Math.max(this.minPanelHeight, zoom * item.display_height + 40);

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
      display_width: 0,
      display_height: 0,
      zoom: 0.5,
      visible: false,
      placed: false,
    };
  }
}
